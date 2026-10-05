// The navy card that leads each screen: one hue, lit from its top-left corner
// and falling away to deep navy, with a soft shadow under it.
//
// Navy in both themes, so everything inside draws with the on-navy tokens
// (theme.navyText, theme.goldHero, …). Content goes in like Card's: a
// ColumnLayout's worth, and the card sizes to it.
//
// The light is a radial gradient, which a Rectangle cannot draw; the Canvas
// paints it once per size change.

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts

Item {
    id: hero

    default property alias content: inner.data
    property real padding: 18
    property real topPadding: padding
    property real bottomPadding: padding
    property real radius: theme.heroRadius
    property bool tappable: false
    // A shadow under the card. Off for a hero nested in a sheet.
    property bool raised: true

    signal tapped()

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: inner.implicitHeight + topPadding + bottomPadding

    // The shadow, behind rather than around the card: a renderer without
    // shader effects (software, the screenshot run) drops the shadow and still
    // draws the card.
    RectangularShadow {
        visible: hero.raised
        anchors.fill: face
        offset.y: 10
        blur: 30
        radius: hero.radius
        color: Qt.rgba(0.008, 0.078, 0.149, theme.light ? 0.3 : 0.55)
    }

    Canvas {
        id: face
        anchors.fill: parent
        antialiasing: true
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            const w = width
            const h = height
            ctx.reset()
            ctx.beginPath()
            ctx.roundedRect(0, 0, w, h, hero.radius, hero.radius)
            ctx.closePath()
            ctx.clip()

            // Deep navy (theme.navy to theme.navyLo), at 165°: from the top a
            // little left of centre, down.
            const base = ctx.createLinearGradient(w * 0.37, 0, w * 0.63, h)
            base.addColorStop(0, "#0D406D")
            base.addColorStop(1, "#062240")
            ctx.fillStyle = base
            ctx.fillRect(0, 0, w, h)

            // The light (theme.navyHi), from the top-left corner.
            const reach = Math.max(w * 1.3, h * 1.1) * 0.62
            const light = ctx.createRadialGradient(0, 0, 0, 0, 0, reach)
            light.addColorStop(0, "rgba(29, 96, 153, 1)")
            light.addColorStop(1, "rgba(29, 96, 153, 0)")
            ctx.fillStyle = light
            ctx.fillRect(0, 0, w, h)

            // A hairline of light along the top edge.
            ctx.fillStyle = "rgba(255, 255, 255, 0.09)"
            ctx.fillRect(hero.radius, 0, w - 2 * hero.radius, 1)
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: hero.radius
        color: tap.pressed ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
    }

    TapHandler {
        id: tap
        enabled: hero.tappable
        onTapped: hero.tapped()
    }

    ColumnLayout {
        id: inner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: hero.padding
        anchors.rightMargin: hero.padding
        anchors.topMargin: hero.topPadding
        spacing: 0
    }
}
