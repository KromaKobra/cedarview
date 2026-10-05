// Every icon in the app, drawn rather than typed. (The app's logo is the one
// exception — it is icon.png, a raster image, which has the same property
// that matters here: it does not depend on the device's fonts.)
//
// This is not a stylistic choice. The toolbar used to say "↻" (U+21BB); the
// desktop font has it, and the phone's Roboto does not, so on a moto g power it
// rendered as a tofu box. Any character outside basic Latin is a per-device
// gamble, and an icon set is exactly where that gamble is worst: a missing
// glyph in a tab bar leaves the user with no idea what the tab is. Canvas is
// part of QtQuick proper, so this needs no font, no QtSvg (which is not in
// the APK's module list) and no image assets.
//
// The drawings are SVG path data on a 24×24 grid — the same strings as the
// v0.4 canvas's inline icons, so a new icon is copied, not redrawn. Context2D
// takes a path as an SVG string directly. Strokes are 1.8 units with round
// ends, scaled with the item, so one drawing serves a 14px chip and a 24px
// tab alike. A part marked `fill` is filled instead (dots, the play button,
// a filled star).

import QtQuick

Canvas {
    id: glyph

    property string kind: "info"
    property color color: "#FFFFFF"
    // In 24ths of the item's size.
    property real stroke: 1.8

    implicitWidth: 20
    implicitHeight: 20
    antialiasing: true

    // Canvas caches its last frame, so a colour change — which is exactly what
    // selecting a tab does — is invisible without an explicit repaint.
    onColorChanged: requestPaint()
    onKindChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    // A circle and a rounded rectangle as path data, since SVG's <circle> and
    // <rect> are elements rather than path commands.
    function circle(cx, cy, r) {
        return "M" + (cx - r) + " " + cy + "a" + r + " " + r + " 0 1 0 " + (2 * r) + " 0"
               + "a" + r + " " + r + " 0 1 0 " + (-2 * r) + " 0z"
    }
    function rect(x, y, w, h, r) {
        return "M" + (x + r) + " " + y + "h" + (w - 2 * r) + "a" + r + " " + r + " 0 0 1 " + r + " " + r
               + "v" + (h - 2 * r) + "a" + r + " " + r + " 0 0 1 " + (-r) + " " + r
               + "h" + (-(w - 2 * r)) + "a" + r + " " + r + " 0 0 1 " + (-r) + " " + (-r)
               + "v" + (-(h - 2 * r)) + "a" + r + " " + r + " 0 0 1 " + r + " " + (-r) + "z"
    }

    // Each icon is a list of parts: a path, or { d, fill: true }.
    function parts(name) {
        switch (name) {
        // ---- The four tabs
        case "today":
            return [rect(3.5, 5, 17, 15.5, 4), "M3.5 10h17M8 3v4M16 3v4", { d: circle(12, 15, 1.6), fill: true }]
        case "chapel":
            return ["M12 2.5v4M10 4.5h4M5 21v-9.5l7-5 7 5V21M10 21v-4.5a2 2 0 0 1 4 0V21M3 21h18"]
        case "dining":
            return ["M7 3v6.5a2.5 2.5 0 0 0 5 0V3M9.5 3v18M17.5 21V3c-2.2 0-3.5 2.6-3.5 6.5 0 2.6 1.2 3.5 3.5 3.5"]
        case "campus":
            return ["M4 21V6.5L11 3v18M11 9l9 3.5V21M2.5 21h19M7 9.5v.01M7 13v.01M7 16.5v.01M15 15v.01M15 18v.01"]
        // ---- Header and sheets
        case "search":
            return [circle(11, 11, 6.5), "M16 16l4.5 4.5"]
        case "more":
            return [{ d: circle(12, 5.5, 1.5), fill: true }, { d: circle(12, 12, 1.5), fill: true },
                    { d: circle(12, 18.5, 1.5), fill: true }]
        case "close":
            return ["M6 6l12 12M18 6L6 18"]
        case "back":
        case "chevronLeft":
            return ["M15 6l-6 6 6 6"]
        case "chevronRight":
            return ["M9 6l6 6-6 6"]
        case "chevronDown":
            return ["M6 9l6 6 6-6"]
        case "chevronUp":
            return ["M6 15l6-6 6 6"]
        // ---- Status
        case "checkCircle":
            return [circle(12, 12, 8.5), "M8.5 12.3l2.3 2.3 4.7-5"]
        case "info":
            return [circle(12, 12, 8.5), "M12 11v5M12 8v.01"]
        case "clock":
            return [circle(12, 12, 8.5), "M12 7.5V12l3 2"]
        case "wifiOff":
            return ["M3 3l18 18M8.5 16.4a5 5 0 0 1 7 0M5 12.9a10 10 0 0 1 4.5-2.5M14.6 10.5A10 10 0 0 1 19 12.9"
                    + "M2 9.4a14.5 14.5 0 0 1 4-2.6M10.5 5.6A14.5 14.5 0 0 1 22 9.4M12 20h.01"]
        case "lock":
            return [rect(5, 10.5, 14, 10, 3), "M8.5 10.5V8a3.5 3.5 0 0 1 7 0v2.5"]
        case "shield":
            return ["M12 3l7.5 3v5.5c0 4.5-3.2 8-7.5 9.5-4.3-1.5-7.5-5-7.5-9.5V6z", "M9 12l2 2 4-4"]
        case "refresh":
            return ["M20 11a8 8 0 1 0-2.3 5.7", "M20 4.5V11h-6.5"]
        // ---- Dining
        case "plate":
            return [circle(12, 12, 8), circle(12, 12, 4)]
        case "card":
            return [rect(3, 6, 18, 13, 3), "M3 10.5h18M7 15h3"]
        case "swap":
            return ["M4 8h14l-3.5-3.5M20 16H6l3.5 3.5"]
        case "leaf":
            return ["M5 19c0-8 5-13 14-14 0 9-5 14-13 14z", "M5 19l7-7"]
        case "pot":
            return ["M4 10h16v6a4 4 0 0 1-4 4H8a4 4 0 0 1-4-4z", "M2 10h20M9 6.5c0-1 1-1.5 1-2.5M13.5 6.5c0-1 1-1.5 1-2.5"]
        // ---- Campus and chapel
        case "moon":
            return ["M20 14.2A8.5 8.5 0 1 1 9.8 4a7 7 0 0 0 10.2 10.2z"]
        case "live":
            return [circle(12, 12, 2),
                    "M8.5 8.5a5 5 0 0 0 0 7M15.5 8.5a5 5 0 0 1 0 7M5.6 5.6a9 9 0 0 0 0 12.8M18.4 5.6a9 9 0 0 1 0 12.8"]
        case "play":
            return [{ d: "M8 5.5v13l11-6.5z", fill: true }]
        case "star":
            return [{ d: "M12 3.8l2.5 5.1 5.6.8-4 3.9 1 5.6-5.1-2.7-5 2.7.9-5.6-4-3.9 5.6-.8z", fill: true }]
        case "starOutline":
            return ["M12 3.8l2.5 5.1 5.6.8-4 3.9 1 5.6-5.1-2.7-5 2.7.9-5.6-4-3.9 5.6-.8z"]
        case "sun":
            return [circle(12, 12, 4), "M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4"
                    + "M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4"]
        case "external":
            return ["M14 4h6v6M20 4l-9 9", "M18 14v4a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4"]
        case "signOut":
            return ["M15 4h3a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-3M10 16l-4-4 4-4M6 12h10"]
        }
        return []
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.scale(width / 24, height / 24)
        ctx.strokeStyle = glyph.color
        ctx.fillStyle = glyph.color
        ctx.lineWidth = glyph.stroke
        ctx.lineCap = "round"
        ctx.lineJoin = "round"

        for (const part of parts(kind)) {
            const filled = typeof part === "object"
            ctx.beginPath()
            ctx.path = filled ? part.d : part
            if (filled)
                ctx.fill()
            else
                ctx.stroke()
        }
    }
}
