// The QML↔C++ contract.
//
// QML is resolved at runtime, so `chapel.refrehAll()` is not a compile error —
// it is a silent no-op on desktop and a line in logcat on the phone, found only
// by pressing the button. The viewmodels are exposed to QML as context
// properties, which is the loosest binding Qt offers and the one with no
// compile-time checking at all.
//
// So: every `chapel.<name>`, `dining.<name>` (and so on) written in the QML
// must resolve to a real property or invokable on the corresponding class.
// Read off the class's staticMetaObject, because that is exactly what QML
// itself looks at — a plain C++ member function is invisible to QML unless it
// is a slot or Q_INVOKABLE, and this test has to fail in that case too.

#include "testsupport.h"

#include "ui/bridge.h"
#include "ui/login.h"
#include "ui/settings.h"
#include "ui/viewmodels/chapel.h"
#include "ui/viewmodels/dining.h"
#include "ui/viewmodels/semester.h"

#include <QMetaMethod>
#include <QMetaProperty>
#include <QRegularExpression>

using namespace mycu;

namespace {

// The context-property name each viewmodel is published under in main.cpp.
const QHash<QString, const QMetaObject *> VIEWMODELS = {
    {QStringLiteral("chapel"), &ChapelViewModel::staticMetaObject},
    {QStringLiteral("dining"), &DiningViewModel::staticMetaObject},
    {QStringLiteral("semester"), &SemesterViewModel::staticMetaObject},
};

// Everything QML binds to by context-property name. `settings` is not a
// viewmodel — it has no provider, no busy flag and no refreshAll — but it is
// bound the same loose way, and by *every* Theme.qml instance, so a typo there
// is the whole app stuck on one palette rather than one broken screen. `login`
// and `bridge` carry the sign-in flow, where a typo means no sign-in at all.
QHash<QString, const QMetaObject *> boundObjects()
{
    QHash<QString, const QMetaObject *> all = VIEWMODELS;
    all.insert(QStringLiteral("settings"), &SettingsController::staticMetaObject);
    all.insert(QStringLiteral("login"), &LoginController::staticMetaObject);
    all.insert(QStringLiteral("bridge"), &Bridge::staticMetaObject);
    return all;
}

QSet<QString> exposedNames(const QMetaObject *meta)
{
    QSet<QString> names;
    for (int i = 0; i < meta->propertyCount(); ++i)
        names.insert(QString::fromLatin1(meta->property(i).name()));
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        names.insert(QString::fromLatin1(method.name()));
        // A signal `fooChanged` is reachable from QML as the handler
        // `onFooChanged` in a Connections block.
        if (method.methodType() == QMetaMethod::Signal) {
            QString handler = QString::fromLatin1(method.name());
            handler[0] = handler[0].toUpper();
            names.insert(QStringLiteral("on") + handler);
        }
    }
    return names;
}

// Comments are stripped, because files name their classes and explain old
// bindings in prose — a mention in a comment is not a live reference.
QString stripComments(const QString &source)
{
    static const QRegularExpression comment(QStringLiteral("//[^\n]*"));
    QString out = source;
    out.remove(comment);
    return out;
}

QStringList qmlFiles()
{
    QDir dir(testing::sourceDir() + QStringLiteral("/qml"));
    QStringList files;
    for (const QString &name : dir.entryList({QStringLiteral("*.qml")}, QDir::Files, QDir::Name))
        files.append(dir.filePath(name));
    return files;
}

} // namespace

class TestQmlContract : public QObject
{
    Q_OBJECT

private slots:
    void thereIsQmlToCheck() { QVERIFY(qmlFiles().size() >= 15); }

    void everyMemberUsedInQmlExists_data()
    {
        QTest::addColumn<QString>("path");
        for (const QString &path : qmlFiles())
            QTest::newRow(qPrintable(QFileInfo(path).fileName())) << path;
    }
    void everyMemberUsedInQmlExists()
    {
        QFETCH(QString, path);
        const auto objects = boundObjects();
        const QRegularExpression reference(
            QStringLiteral("\\b(%1)\\.(\\w+)").arg(QStringList(objects.keys()).join(u'|')));

        const QString source = stripComments(testing::readText(path));
        auto it = reference.globalMatch(source);
        while (it.hasNext()) {
            const auto match = it.next();
            // A string literal like "…/PRIVACY.md" or "sign-in.microsoft" is
            // not a binding; only look at matches outside quotes.
            const qsizetype lineStart = source.lastIndexOf(u'\n', match.capturedStart()) + 1;
            if (source.mid(lineStart, match.capturedStart() - lineStart).count(u'"') % 2 == 1)
                continue;
            const QString object = match.captured(1);
            const QString member = match.captured(2);
            QVERIFY2(exposedNames(objects.value(object)).contains(member),
                     qPrintable(QStringLiteral("%1 uses `%2.%3`, which is not a property or invokable on %4. "
                                               "QML would fail at runtime, not here.")
                                    .arg(QFileInfo(path).fileName(), object, member,
                                         QString::fromLatin1(objects.value(object)->className()))));
        }
    }

    // Both screens draw on more than one provider; refresh must cover them
    // all.
    //
    // A regression test. refreshCurrent() in Main.qml and both pull-to-refresh
    // handlers used to call refresh(), which on the Dining screen fetches the
    // public menu API and *not* the meal-plan balances — so the meals-left and
    // dollar figures loaded once at sign-in and never moved again, no matter
    // how hard you pulled.
    void theRefreshGestureReachesEverySourceOnTheScreen()
    {
        for (const char *name : {"Main.qml", "ChapelView.qml", "DiningView.qml", "ChucksView.qml", "SummaryView.qml"}) {
            const QString source =
                stripComments(testing::readText(testing::sourceDir() + "/qml/" + QLatin1StringView(name)));
            for (const QString &vm : VIEWMODELS.keys()) {
                const QRegularExpression bare(QStringLiteral("\\b%1\\.refresh\\(\\)").arg(vm));
                QVERIFY2(!bare.match(source).hasMatch(),
                         qPrintable(QStringLiteral("%1 calls `%2.refresh()` as a user-facing refresh. Use "
                                                   "`%2.refreshAll()`, which also reloads the other source on "
                                                   "that screen.")
                                        .arg(QLatin1StringView(name), vm)));
            }
        }
    }

    void refreshAllExistsOnEveryViewmodel()
    {
        for (auto it = VIEWMODELS.cbegin(); it != VIEWMODELS.cend(); ++it)
            QVERIFY2(exposedNames(it.value()).contains(QStringLiteral("refreshAll")), qPrintable(it.key()));
    }

    // The list models' role names are part of the contract too: a delegate
    // reading `model.whenText` gets undefined, silently, if the role is
    // renamed on the C++ side.
    void everyRoleADelegateReadsExists()
    {
        QSet<QString> roles;
        for (QAbstractItemModel *model :
             std::initializer_list<QAbstractItemModel *>{new ChapelListModel, new ScheduleListModel,
                                                         new MenuListModel, new ActivityListModel}) {
            for (const QByteArray &name : model->roleNames())
                roles.insert(QString::fromLatin1(name));
            delete model;
        }
        static const QRegularExpression modelRef(QStringLiteral("\\bmodel\\.(\\w+)"));
        for (const QString &path : qmlFiles()) {
            auto it = modelRef.globalMatch(stripComments(testing::readText(path)));
            while (it.hasNext()) {
                const QString role = it.next().captured(1);
                if (role == QStringLiteral("index") || role == QStringLiteral("modelData"))
                    continue;
                QVERIFY2(roles.contains(role),
                         qPrintable(QStringLiteral("%1 reads model.%2, which no list model provides")
                                        .arg(QFileInfo(path).fileName(), role)));
            }
        }
    }
};

CEDARVIEW_TEST_MAIN(TestQmlContract, QCoreApplication)
#include "tst_qml_contract.moc"
