// The architectural rule, enforced.
//
// src/core must not depend on anything above Qt Core — no Gui, no Quick, no
// Qml, no WebEngine or WebView. This is the seam that lets every parsing test
// run with no display and no network, and it is exactly the kind of rule that
// erodes silently unless something fails when it is broken.
//
// It is enforced twice. The build does the heavy lifting: cedarview_core links
// Qt6::Core only (src/core/CMakeLists.txt refuses to configure otherwise), so
// a `#include <QGuiApplication>` in the core cannot even find its header. This
// test covers what the include paths cannot see: a module-qualified include
// like `<QtGui/QColor>` resolves through the shared include root on some
// installations, and would compile and then fail only at link time — or, in a
// header-only use, not at all.

#include "testsupport.h"

#include <QDirIterator>
#include <QRegularExpression>

class TestCoreIsGuiFree : public QObject
{
    Q_OBJECT

private:
    static QStringList coreSources()
    {
        QStringList files;
        QDirIterator it(testing::sourceDir() + QStringLiteral("/src/core"),
                        {QStringLiteral("*.h"), QStringLiteral("*.cpp")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
            files.append(it.next());
        files.sort();
        return files;
    }

private slots:
    void thereIsSomethingToCheck() { QVERIFY2(coreSources().size() > 10, "has the layout moved?"); }

    void theCoreLibraryLinksQtCoreOnly()
    {
        const QString libs = testing::readText(QStringLiteral(CEDARVIEW_BINARY_DIR "/core_link_libraries.txt"));
        QCOMPARE(libs.trimmed(), QStringLiteral("Qt6::Core"));
    }

    void noGuiIncludeAnywhereInTheCore()
    {
        // Built from pieces rather than one raw string: moc's tokenizer loses
        // its place in a raw string holding `#include` and a lone quote.
        const QString include = QStringLiteral("#\\s*include\\s*[<\\x22]");
        static const QRegularExpression forbidden(
            include
            + QStringLiteral("(Qt(Gui|Quick\\w*|Qml\\w*|Widgets|WebEngine\\w*|WebView)/|"
                             "Q(GuiApplication|QmlEngine|QmlContext|QuickItem|QuickView|JSValue|JSEngine|"
                             "Color|Image|Pixmap|Font)\\b)"));

        QStringList offenders;
        for (const QString &path : coreSources()) {
            const QString text = testing::readText(path);
            const QRegularExpressionMatch m = forbidden.match(text);
            if (m.hasMatch())
                offenders.append(QFileInfo(path).fileName() + QStringLiteral(": ") + m.captured());
        }
        QVERIFY2(offenders.isEmpty(),
                 qPrintable(QStringLiteral("src/core must stay GUI-free — move this into src/ui: ")
                            + offenders.join(QStringLiteral("; "))));
    }

    // Qt Network is allowed in exactly one file: the HTTP transport, which is
    // its own library (cedarview_http) precisely so the rest of the core does
    // not link it.
    void networkingStaysInTheHttpTransport()
    {
        static const QRegularExpression network(
            QStringLiteral("#\\s*include\\s*[<\\x22](QtNetwork/|QNetwork\\w*|QSsl\\w*|QTcp\\w*)"));
        for (const QString &path : coreSources()) {
            if (QFileInfo(path).baseName() == QStringLiteral("httptransport"))
                continue;
            QVERIFY2(!network.match(testing::readText(path)).hasMatch(), qPrintable(path));
        }
    }
};

CEDARVIEW_TEST_MAIN(TestCoreIsGuiFree, QCoreApplication)
#include "tst_core_is_gui_free.moc"
