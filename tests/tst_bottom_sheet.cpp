// BottomSheet, run for real: opened, closed and opened again.
//
// The sheets (More, a chapel's details) once rose only the first time. The
// closing transition animated `y` and so replaced its binding, and every
// opening after that rose to where the closing had left the sheet — below
// the bottom edge, with the scrim dimming the screen over nothing. A QML
// runtime fault, so it takes a QML runtime to catch: the sheet is loaded
// straight from qml/ and its position read back.

#include "testsupport.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

namespace {

const QByteArray HOST = R"(
import QtQuick
import "file:)" CEDARVIEW_SOURCE_DIR R"(/qml"

Window {
    width: 360
    height: 640
    visible: true
    property alias sheet: sheet

    BottomSheet {
        id: sheet
        Rectangle { implicitHeight: 120; implicitWidth: 100 }
    }
}
)";

} // namespace

class TestBottomSheet : public QObject
{
    Q_OBJECT

private:
    QQmlEngine m_engine;
    std::unique_ptr<QObject> m_window;

    QObject *sheet() const { return m_window->property("sheet").value<QObject *>(); }

    // Where a sheet that has finished opening sits: flush with the bottom.
    qreal restingY() const
    {
        return m_window->property("height").toReal() - sheet()->property("height").toReal();
    }

    void openAndSettle()
    {
        QMetaObject::invokeMethod(sheet(), "open");
        QTRY_VERIFY(sheet()->property("opened").toBool());
        QTRY_COMPARE(sheet()->property("y").toReal(), restingY());
    }

    void closeAndSettle()
    {
        QMetaObject::invokeMethod(sheet(), "close");
        QTRY_VERIFY(!sheet()->property("visible").toBool());
    }

private slots:
    void init()
    {
        QQmlComponent component(&m_engine);
        component.setData(HOST, QUrl(QStringLiteral("file:" CEDARVIEW_SOURCE_DIR "/tests/sheet-host.qml")));
        m_window.reset(component.create());
        QVERIFY2(m_window, qPrintable(component.errorString()));
        QVERIFY(sheet());
    }

    void cleanup() { m_window.reset(); }

    void theSheetRisesToTheBottomEdge()
    {
        openAndSettle();
        QVERIFY(restingY() > 0);
    }

    void theSheetRisesAgainAfterClosing()
    {
        openAndSettle();
        closeAndSettle();
        openAndSettle();
        closeAndSettle();
        openAndSettle();
    }
};

CEDARVIEW_TEST_MAIN(TestBottomSheet, QGuiApplication)
#include "tst_bottom_sheet.moc"
