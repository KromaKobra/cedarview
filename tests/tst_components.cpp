// QML components, run for real.
//
// The faults these guard against are QML runtime faults, so it takes a QML
// runtime to catch them: each component is loaded straight from qml/ into a
// window and its geometry or focus read back.
//
// * BottomSheet once rose only the first time. The closing transition
//   animated `y` and so replaced its binding, and every opening after that
//   rose to where the closing had left the sheet — below the bottom edge,
//   with the scrim dimming the screen over nothing.
// * SegmentedControl's labels sat off centre: they were padded by half of
//   (height − implicitHeight), and a Column's implicitHeight counts its own
//   padding.
// * SearchBox's text was covered by its own border on the phone, where the
//   Controls style (Material) floats TextField's placeholder up onto the top
//   edge; and a field that had been hidden kept its focus, and on Android its
//   cursor handle.
// * A component named like a Qt Quick Controls type is shadowed by it in any
//   file that imports Controls — Page once, SearchField (new in Qt 6.10) since.
//   The host resolves names the way the app's own files do, so a clash fails
//   to load here.

#include "testsupport.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

namespace {

const QByteArray HOST = R"(
import QtQuick
import QtQuick.Controls

Window {
    width: 360
    height: 640
    visible: true
    property alias sheet: sheet
    property alias segments: segments
    property alias sittings: sittings
    property alias field: field

    Column {
        width: parent.width
        spacing: 12

        SegmentedControl {
            id: segments
            width: 320
            labels: ["Plan", "Menu", "Hours"]
        }
        SegmentedControl {
            id: sittings
            width: 320
            labels: ["Breakfast", "Lunch"]
            sublabels: ["7–9:30", "10:30–2"]
        }
        SearchBox {
            id: field
            width: 320
            placeholder: "Search menus, places, speakers"
        }
    }

    BottomSheet {
        id: sheet
        Rectangle { implicitHeight: 120; implicitWidth: 100 }
    }
}
)";

// Every Text under `root`, depth first.
QList<QQuickItem *> texts(QQuickItem *root)
{
    QList<QQuickItem *> out;
    for (QQuickItem *child : root->childItems()) {
        if (child->inherits("QQuickText"))
            out.append(child);
        out.append(texts(child));
    }
    return out;
}

QQuickItem *textReading(QQuickItem *root, const QString &value)
{
    for (QQuickItem *text : texts(root)) {
        if (text->property("text").toString() == value)
            return text;
    }
    return nullptr;
}

// Where `item`'s vertical middle falls in `in`. Centring snaps to whole
// pixels, so "centred" below means within one.
qreal middleIn(QQuickItem *item, QQuickItem *in)
{
    return item->mapToItem(in, QPointF(0, item->height() / 2)).y();
}

} // namespace

class TestComponents : public QObject
{
    Q_OBJECT

private:
    QQmlEngine m_engine;
    std::unique_ptr<QObject> m_window;

    template<typename T = QObject>
    T *part(const char *name) const
    {
        return qobject_cast<T *>(m_window->property(name).value<QObject *>());
    }

    // ---- BottomSheet

    // Where a sheet that has finished opening sits: flush with the bottom.
    qreal restingY() const
    {
        return m_window->property("height").toReal() - part("sheet")->property("height").toReal();
    }

    void openAndSettle()
    {
        QMetaObject::invokeMethod(part("sheet"), "open");
        QTRY_VERIFY(part("sheet")->property("opened").toBool());
        QTRY_COMPARE(part("sheet")->property("y").toReal(), restingY());
    }

    void closeAndSettle()
    {
        QMetaObject::invokeMethod(part("sheet"), "close");
        QTRY_VERIFY(!part("sheet")->property("visible").toBool());
    }

private slots:
    void init()
    {
        QQmlComponent component(&m_engine);
        // As if the host were a file in qml/: its components come in through
        // the implicit import of their own directory, the weakest there is, as
        // they do for the app's files — so a Qt Quick Controls type of the
        // same name shadows one here just as it would there.
        component.setData(HOST, QUrl(QStringLiteral("file:" CEDARVIEW_SOURCE_DIR "/qml/ComponentsHost.qml")));
        m_window.reset(component.create());
        QVERIFY2(m_window, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(m_window.get());
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }

    void cleanup() { m_window.reset(); }

    // ---- BottomSheet ---------------------------------------------------------------

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

    // ---- SegmentedControl ----------------------------------------------------------

    void segmentLabelsAreCentredVertically()
    {
        auto *segments = part<QQuickItem>("segments");
        QVERIFY(segments->height() > 0);
        for (const QString &label : {QStringLiteral("Plan"), QStringLiteral("Menu"), QStringLiteral("Hours")}) {
            QQuickItem *text = textReading(segments, label);
            QVERIFY2(text, qPrintable(label));
            QVERIFY2(qAbs(middleIn(text, segments) - segments->height() / 2) < 1,
                     qPrintable(QStringLiteral("%1 is centred at %2 in a %3 px control")
                                    .arg(label)
                                    .arg(middleIn(text, segments))
                                    .arg(segments->height())));
        }
    }

    // With hours under the sittings, the two lines are centred as one.
    void aLabelAndItsSublabelAreCentredTogether()
    {
        auto *sittings = part<QQuickItem>("sittings");
        QQuickItem *label = textReading(sittings, QStringLiteral("Lunch"));
        QQuickItem *hours = textReading(sittings, QStringLiteral("10:30–2"));
        QVERIFY(label && hours);
        const qreal top = label->mapToItem(sittings, QPointF(0, 0)).y();
        const qreal bottom = hours->mapToItem(sittings, QPointF(0, hours->height())).y();
        QVERIFY2(qAbs((top + bottom) / 2 - sittings->height() / 2) < 1,
                 qPrintable(QStringLiteral("lines span %1–%2 in a %3 px control")
                                .arg(top)
                                .arg(bottom)
                                .arg(sittings->height())));
    }

    // ---- SearchBox ---------------------------------------------------------------

    // The style's placeholder is never used, so it can never float onto the
    // border; ours goes with the first letter.
    void thePlaceholderIsOursAndGoesWithTheFirstLetter()
    {
        auto *field = part<QQuickItem>("field");
        QCOMPARE(field->property("placeholderText").toString(), QString());
        QQuickItem *placeholder = textReading(field, QStringLiteral("Search menus, places, speakers"));
        QVERIFY(placeholder);
        QVERIFY(placeholder->isVisible());
        QVERIFY(qAbs(middleIn(placeholder, field) - field->height() / 2) < 1);

        field->setProperty("text", QStringLiteral("pi"));
        QVERIFY(!placeholder->isVisible());
        field->setProperty("text", QString());
        QVERIFY(placeholder->isVisible());
    }

    // Hiding a field leaves it the focus object — which is what keeps
    // Android's cursor handle on screen. release() hands focus back.
    void releaseLetsGoOfTheFocus()
    {
        auto *field = part<QQuickItem>("field");
        auto *window = qobject_cast<QQuickWindow *>(m_window.get());
        field->forceActiveFocus();
        QTRY_VERIFY(field->hasActiveFocus());

        field->setVisible(false);
        QVERIFY(field->hasActiveFocus()); // the trap

        QMetaObject::invokeMethod(field, "release");
        QVERIFY(!field->hasActiveFocus());
        QVERIFY(window->activeFocusItem() != field);
    }
};

CEDARVIEW_TEST_MAIN(TestComponents, QGuiApplication)
#include "tst_components.moc"
