// A progress bar strung between two squircles.
//
// The squircles are the same size and carry the figures: `startText` over
// `startNote` on the left, `endText` over `endNote` on the right. A thin groove
// joins them through concave fillets, so the bar flares into each end rather
// than butting into it, and `title` sits centred just above the groove.
//
// The left squircle is always filled; the fill then runs along the groove to
// `fraction`. Fill and groove are each one blue or one grey, shaded top to
// bottom — lit and raised for the fill, sunken for the groove — so the bar has
// depth without ever changing hue.
//
// The outline is sampled in JS into a polyline: Shapes has no superellipse
// primitive, and the curve renderer antialiases a polyline as cleanly as a path.

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes

Item {
    id: meter

    //: 0.0–1.0, how far along the fill is. Clamped here as well as in the
    //: viewmodels, because a fill that overruns its track looks broken.
    property real fraction: 0.0
    property string title
    property string startText
    property string startNote
    property string endText
    property string endNote

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: 64

    readonly property real neck: 8
    readonly property real fillet: 9
    //: Centre line of the groove, a little below the middle so the title above
    //: it and the empty space below it balance.
    readonly property real neckY: height / 2 + 6
    readonly property real exponent: 5

    // Every fraction starts at 0 and jumps to its real value on the first
    // frame. Animating that makes it read as the bar filling rather than as
    // the screen twitching.
    property real shown: Math.max(0.0, Math.min(1.0, fraction))
    Behavior on shown { NumberAnimation { duration: 520; easing.type: Easing.OutCubic } }

    // Where the fillets meet each squircle's inner side, as offsets from the
    // squircle's centre.
    readonly property real half: height / 2
    readonly property real joinTop: neckY - neck / 2 - fillet - half
    readonly property real joinBottom: neckY + neck / 2 + fillet - half
    readonly property real neckStart: half + squircleReach(joinTop) + fillet
    readonly property real neckEnd: width - neckStart
    readonly property real fillX: neckStart + Math.max(0, neckEnd - neck / 2 - neckStart) * shown

    // Horizontal distance from a squircle's centre to its edge at height dy.
    function squircleReach(dy) {
        return half * Math.pow(1 - Math.pow(Math.abs(dy) / half, exponent), 1 / exponent)
    }

    // The angle on a squircle's right side at which its edge sits at height dy.
    function sideAngle(dy) {
        return Math.asin(Math.sign(dy) * Math.pow(Math.abs(dy) / half, exponent / 2))
    }

    function squircleArc(pts, cx, from, to) {
        const steps = 48
        const e = 2 / exponent
        for (let i = 0; i <= steps; ++i) {
            const t = from + (to - from) * i / steps
            const c = Math.cos(t), s = Math.sin(t)
            pts.push(Qt.point(cx + half * Math.sign(c) * Math.pow(Math.abs(c), e),
                              half + half * Math.sign(s) * Math.pow(Math.abs(s), e)))
        }
    }

    function quad(pts, x0, y0, cx, cy, x1, y1) {
        const steps = 10
        for (let i = 0; i <= steps; ++i) {
            const t = i / steps, u = 1 - t
            pts.push(Qt.point(u * u * x0 + 2 * u * t * cx + t * t * x1,
                              u * u * y0 + 2 * u * t * cy + t * t * y1))
        }
    }

    //: The full outline when `toX` is undefined; otherwise the left squircle
    //: plus the groove up to a round cap centred on `toX`.
    function outline(toX) {
        const pts = []
        if (width <= height * 2)
            return pts
        const top = neckY - neck / 2, bottom = neckY + neck / 2
        const lT = half + squircleReach(joinTop), lB = half + squircleReach(joinBottom)
        const rT = width - lT, rB = width - lB
        const aT = sideAngle(joinTop), aB = sideAngle(joinBottom)

        squircleArc(pts, half, aB, 2 * Math.PI + aT)
        quad(pts, lT, half + joinTop, lT, top, lT + fillet, top)
        if (toX === undefined) {
            quad(pts, rT - fillet, top, rT, top, rT, half + joinTop)
            squircleArc(pts, width - half, Math.PI - aT, 3 * Math.PI - aB)
            quad(pts, rB, half + joinBottom, rB, bottom, rB - fillet, bottom)
        } else {
            const r = neck / 2
            for (let i = 0; i <= 12; ++i) {
                const t = -Math.PI / 2 + Math.PI * i / 12
                pts.push(Qt.point(toX + r * Math.cos(t), neckY + r * Math.sin(t)))
            }
        }
        quad(pts, lB + fillet, bottom, lB, bottom, lB, half + joinBottom)
        return pts
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            strokeWidth: 0
            fillGradient: LinearGradient {
                x1: 0; y1: 0; x2: 0; y2: meter.height
                GradientStop { position: 0.0; color: theme.grooveTop }
                GradientStop { position: 1.0; color: theme.grooveBottom }
            }
            PathPolyline { path: meter.outline() }
        }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: theme.meterGlow
            shadowOpacity: theme.light ? 0.30 : 0.45
            shadowBlur: 0.55
            shadowVerticalOffset: 3
        }

        ShapePath {
            strokeColor: "transparent"
            strokeWidth: 0
            fillGradient: LinearGradient {
                x1: 0; y1: 0; x2: 0; y2: meter.height
                GradientStop { position: 0.0; color: theme.meterTop }
                GradientStop { position: 0.5; color: theme.meterMid }
                GradientStop { position: 1.0; color: theme.meterBottom }
            }
            PathPolyline { path: meter.outline(meter.fillX) }
        }
    }

    Column {
        anchors.horizontalCenter: parent.left
        anchors.horizontalCenterOffset: meter.half
        anchors.verticalCenter: parent.verticalCenter

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: meter.startText
            color: theme.textOnCedar
            font.pixelSize: 21
            font.bold: true
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: meter.startNote
            color: theme.textOnCedar
            opacity: 0.78
            font.pixelSize: 10
            font.bold: true
        }
    }

    Column {
        anchors.horizontalCenter: parent.right
        anchors.horizontalCenterOffset: -meter.half
        anchors.verticalCenter: parent.verticalCenter

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: meter.endText
            color: theme.text
            font.pixelSize: 21
            font.bold: true
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: meter.endNote
            color: theme.muted
            font.pixelSize: 10
            font.bold: true
        }
    }

    Label {
        x: meter.neckStart
        width: meter.neckEnd - meter.neckStart
        y: meter.neckY - meter.neck / 2 - height - 6
        horizontalAlignment: Text.AlignHCenter
        text: meter.title
        color: theme.text
        font.pixelSize: 14
        font.bold: true
        elide: Text.ElideRight
    }
}
