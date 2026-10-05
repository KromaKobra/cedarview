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
#include "ui/sync.h"
#include "ui/viewmodels/campus.h"
#include "ui/viewmodels/chapel.h"
#include "ui/viewmodels/curfew.h"
#include "ui/viewmodels/dining.h"
#include "ui/viewmodels/hours.h"
#include "ui/viewmodels/search.h"
#include "ui/viewmodels/sourcestatus.h"
#include "ui/viewmodels/today.h"

#include <QMetaMethod>
#include <QMetaProperty>
#include <QRegularExpression>

using namespace mycu;

namespace {

// The context-property name each viewmodel is published under in main.cpp.
// All of them re-read the clock through refreshAll(), which is what
// SyncCoordinator's tick calls.
const QHash<QString, const QMetaObject *> VIEWMODELS = {
    {QStringLiteral("chapel"), &ChapelViewModel::staticMetaObject},
    {QStringLiteral("dining"), &DiningViewModel::staticMetaObject},
    {QStringLiteral("curfew"), &CurfewViewModel::staticMetaObject},
    {QStringLiteral("hours"), &HoursViewModel::staticMetaObject},
    {QStringLiteral("campus"), &CampusViewModel::staticMetaObject},
    {QStringLiteral("today"), &TodayViewModel::staticMetaObject},
};

// Everything QML binds to by context-property name. `settings` is not a
// viewmodel — it has no provider and no refreshAll — but it is bound the same
// loose way, and by *every* Theme.qml instance, so a typo there is the whole
// app stuck on one palette rather than one broken screen. `login` and `bridge`
// carry the sign-in flow, where a typo means no sign-in at all; `sync` every
// refresh; `search` the search page.
QHash<QString, const QMetaObject *> boundObjects()
{
    QHash<QString, const QMetaObject *> all = VIEWMODELS;
    all.insert(QStringLiteral("settings"), &SettingsController::staticMetaObject);
    all.insert(QStringLiteral("login"), &LoginController::staticMetaObject);
    all.insert(QStringLiteral("bridge"), &Bridge::staticMetaObject);
    all.insert(QStringLiteral("sync"), &SyncCoordinator::staticMetaObject);
    all.insert(QStringLiteral("search"), &SearchViewModel::staticMetaObject);
    return all;
}

// Every list model a delegate reads, for their role names.
QSet<QString> allRoles()
{
    QSet<QString> roles;
    const std::initializer_list<QAbstractItemModel *> models{
        new ChapelListModel, new ScheduleListModel, new MenuListModel,    new StationListModel,
        new ActivityListModel, new TimelineModel,   new AgendaModel,      new SearchResultModel,
    };
    for (QAbstractItemModel *model : models) {
        for (const QByteArray &name : model->roleNames())
            roles.insert(QString::fromLatin1(name));
        delete model;
    }
    return roles;
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

    // Every refresh goes through the coordinator.
    //
    // It started as a regression test: pull-to-refresh on the Dining screen
    // called `dining.refresh()`, which fetches the public menu and *not* the
    // meal-plan balances, so the meals-left and dollar figures never moved no
    // matter how hard you pulled. Since v0.4 SyncCoordinator is the one place
    // that knows what a refresh takes (and what is already in flight, and
    // whether a silent sign-in is the real answer); a screen that called a
    // viewmodel directly would go around it.
    void everyRefreshGoesThroughTheCoordinator()
    {
        static const QRegularExpression direct(
            QStringLiteral("\\b(%1)\\.(refresh\\w*)\\(").arg(QStringList(VIEWMODELS.keys()).join(u'|')));
        for (const QString &path : qmlFiles()) {
            const auto match = direct.match(stripComments(testing::readText(path)));
            QVERIFY2(!match.hasMatch(),
                     qPrintable(QStringLiteral("%1 calls `%2.%3()` itself. Use `sync.refreshAll()`, which "
                                               "covers every source and the sign-in.")
                                    .arg(QFileInfo(path).fileName(), match.captured(1), match.captured(2))));
        }
        const QString page = stripComments(testing::readText(testing::sourceDir() + "/qml/ScrollPage.qml"));
        QVERIFY2(page.contains("sync.refreshAll()"), "pull-to-refresh must ask the coordinator");
    }

    // `chapel.skipsStatus.hasData` and friends: a status is a SourceStatus,
    // reached from a viewmodel by name — directly, or through a local
    // `property var skips: chapel.skipsStatus`. Both halves are checked.
    void everyStatusFieldUsedInQmlExists_data() { everyMemberUsedInQmlExists_data(); }
    void everyStatusFieldUsedInQmlExists()
    {
        QFETCH(QString, path);
        const QString source = stripComments(testing::readText(path));
        const QSet<QString> fields = exposedNames(&SourceStatus::staticMetaObject);
        const auto objects = boundObjects();

        auto check = [&](const QString &vm, const QString &status, const QString &field) {
            QVERIFY2(exposedNames(objects.value(vm)).contains(status),
                     qPrintable(QStringLiteral("%1: %2 has no %3").arg(QFileInfo(path).fileName(), vm, status)));
            QVERIFY2(fields.contains(field),
                     qPrintable(QStringLiteral("%1 reads `%2.%3.%4`, which SourceStatus does not have")
                                    .arg(QFileInfo(path).fileName(), vm, status, field)));
        };

        static const QRegularExpression direct(QStringLiteral("\\b(chapel|dining)\\.(\\w+Status)\\.(\\w+)"));
        auto it = direct.globalMatch(source);
        while (it.hasNext()) {
            const auto m = it.next();
            check(m.captured(1), m.captured(2), m.captured(3));
        }

        static const QRegularExpression alias(
            QStringLiteral("property\\s+var\\s+(\\w+)\\s*:\\s*(chapel|dining)\\.(\\w+Status)\\b"));
        auto aliases = alias.globalMatch(source);
        while (aliases.hasNext()) {
            const auto a = aliases.next();
            const QRegularExpression use(QStringLiteral("\\b%1\\.(\\w+)").arg(a.captured(1)));
            auto uses = use.globalMatch(source);
            while (uses.hasNext())
                check(a.captured(2), a.captured(3), uses.next().captured(1));
        }
    }

    // What SyncCoordinator::tick() relies on, for the ones that follow the
    // clock.
    void refreshAllExistsOnEveryViewmodel()
    {
        for (auto it = VIEWMODELS.cbegin(); it != VIEWMODELS.cend(); ++it) {
            const QSet<QString> names = exposedNames(it.value());
            QVERIFY2(names.contains(QStringLiteral("refreshAll")) || names.contains(QStringLiteral("tick")),
                     qPrintable(it.key()));
        }
    }

    // The list models' role names are part of the contract too: a delegate
    // reading `model.whenText` gets undefined, silently, if the role is
    // renamed on the C++ side.
    void everyRoleADelegateReadsExists()
    {
        const QSet<QString> roles = allRoles();
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

    // A delegate's `required property` is filled from the role of that name,
    // and one with no such role stops the delegate being created at all — the
    // list just stays empty. Components' own required properties (none, so
    // far) would need listing here.
    void everyRequiredPropertyIsARole()
    {
        const QSet<QString> roles = allRoles();
        static const QRegularExpression required(QStringLiteral("required\\s+property\\s+\\w+\\s+(\\w+)"));
        for (const QString &path : qmlFiles()) {
            auto it = required.globalMatch(stripComments(testing::readText(path)));
            while (it.hasNext()) {
                const QString name = it.next().captured(1);
                if (name == QStringLiteral("index") || name == QStringLiteral("modelData"))
                    continue;
                QVERIFY2(roles.contains(name),
                         qPrintable(QStringLiteral("%1 requires `%2`, which no list model provides")
                                        .arg(QFileInfo(path).fileName(), name)));
            }
        }
    }
};

CEDARVIEW_TEST_MAIN(TestQmlContract, QCoreApplication)
#include "tst_qml_contract.moc"
