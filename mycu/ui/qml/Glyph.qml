// Every icon in the app, drawn rather than typed.
//
// This is not a stylistic choice. The toolbar used to say "↻" (U+21BB); the
// desktop font has it, and the phone's Roboto does not, so on a moto g power it
// rendered as a tofu box — see the note that used to live in Main.qml. Any
// character outside basic Latin is a per-device gamble, and an icon set is
// exactly where that gamble is worst: a missing glyph in body text is ugly,
// but a missing glyph in a tab bar leaves the user with no idea what the tab
// is. Canvas is part of QtQuick proper, so this needs no font, no QtSvg (which
// is not in the APK's module list) and no image assets.
//
// Coordinates are fractions of the item's own size, so one drawing serves the
// 22px tab icons and the 26px logo alike.

import QtQuick

Canvas {
    id: glyph

    //: "tree" | "refresh" | "summary" | "chapel" | "dining"
    property string kind: "tree"
    property color color: "#FFFFFF"

    implicitWidth: 22
    implicitHeight: 22
    antialiasing: true

    // Canvas caches its last frame, so a colour change — which is exactly what
    // selecting a tab does — is invisible without an explicit repaint.
    onColorChanged: requestPaint()
    onKindChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        const w = width
        const h = height
        ctx.reset()
        ctx.strokeStyle = glyph.color
        ctx.fillStyle = glyph.color
        ctx.lineCap = "round"
        ctx.lineJoin = "round"

        switch (kind) {
        case "tree":    paintTree(ctx, w, h);    break
        case "refresh": paintRefresh(ctx, w, h); break
        case "summary": paintSummary(ctx, w, h); break
        case "chapel":  paintChapel(ctx, w, h);  break
        case "dining":  paintDining(ctx, w, h);  break
        }
    }

    // ---- The mark: a cedar, filled ---------------------------------------
    function paintTree(ctx, w, h) {
        ctx.beginPath()
        ctx.moveTo(0.50 * w, 0.04 * h)
        ctx.lineTo(0.79 * w, 0.45 * h)
        ctx.lineTo(0.21 * w, 0.45 * h)
        ctx.closePath()
        ctx.fill()

        ctx.beginPath()
        ctx.moveTo(0.50 * w, 0.28 * h)
        ctx.lineTo(0.93 * w, 0.80 * h)
        ctx.lineTo(0.07 * w, 0.80 * h)
        ctx.closePath()
        ctx.fill()

        ctx.fillRect(0.43 * w, 0.78 * h, 0.14 * w, 0.20 * h)
    }

    // ---- Refresh: an open circle with a head on the open end --------------
    function paintRefresh(ctx, w, h) {
        const cx = 0.5 * w
        const cy = 0.52 * h
        const r = 0.34 * Math.min(w, h)
        const start = -0.42 * Math.PI
        const stroke = 0.115 * Math.min(w, h)

        ctx.lineWidth = stroke
        ctx.beginPath()
        ctx.arc(cx, cy, r, start, 1.30 * Math.PI, false)
        ctx.stroke()

        // Tip along the tangent, base across the stroke, so the head reads as
        // part of the same line rather than a triangle parked beside it.
        const size = 0.20 * Math.min(w, h)
        const px = cx + r * Math.cos(start)
        const py = cy + r * Math.sin(start)
        const tx = Math.sin(start)
        const ty = -Math.cos(start)
        const nx = Math.cos(start)
        const ny = Math.sin(start)

        ctx.beginPath()
        ctx.moveTo(px + tx * size * 1.1, py + ty * size * 1.1)
        ctx.lineTo(px - nx * size * 0.85, py - ny * size * 0.85)
        ctx.lineTo(px + nx * size * 0.85, py + ny * size * 0.85)
        ctx.closePath()
        ctx.fill()
    }

    // ---- Summary: bars ----------------------------------------------------
    function paintSummary(ctx, w, h) {
        const base = 0.84 * h
        const bw = 0.17 * w
        const tops = [0.46, 0.20, 0.58]
        const xs = [0.15, 0.415, 0.68]
        for (let i = 0; i < 3; ++i) {
            ctx.fillRect(xs[i] * w, tops[i] * h, bw, base - tops[i] * h)
        }
    }

    // ---- Chapel: a roof, walls and an arched door -------------------------
    function paintChapel(ctx, w, h) {
        ctx.lineWidth = 0.095 * Math.min(w, h)

        ctx.beginPath()
        ctx.moveTo(0.10 * w, 0.46 * h)
        ctx.lineTo(0.50 * w, 0.13 * h)
        ctx.lineTo(0.90 * w, 0.46 * h)
        ctx.stroke()

        ctx.beginPath()
        ctx.moveTo(0.20 * w, 0.44 * h)
        ctx.lineTo(0.20 * w, 0.88 * h)
        ctx.lineTo(0.80 * w, 0.88 * h)
        ctx.lineTo(0.80 * w, 0.44 * h)
        ctx.stroke()

        // Filled, not stroked. At the 18px the tab bar draws this at, a
        // stroked doorway is three hairlines that merge into a smudge; a solid
        // one still reads as a door.
        ctx.beginPath()
        ctx.arc(0.50 * w, 0.68 * h, 0.13 * w, Math.PI, 2 * Math.PI, false)
        ctx.lineTo(0.63 * w, 0.88 * h)
        ctx.lineTo(0.37 * w, 0.88 * h)
        ctx.closePath()
        ctx.fill()
    }

    // ---- Dining: fork and knife -------------------------------------------
    function paintDining(ctx, w, h) {
        ctx.lineWidth = 0.09 * Math.min(w, h)

        // Fork: three tines onto a shoulder, then the handle.
        for (const x of [0.22, 0.34, 0.46]) {
            ctx.beginPath()
            ctx.moveTo(x * w, 0.12 * h)
            ctx.lineTo(x * w, 0.34 * h)
            ctx.stroke()
        }
        ctx.beginPath()
        ctx.moveTo(0.22 * w, 0.34 * h)
        ctx.lineTo(0.46 * w, 0.34 * h)
        ctx.moveTo(0.34 * w, 0.34 * h)
        ctx.lineTo(0.34 * w, 0.90 * h)
        ctx.stroke()

        // Knife: a tapered blade over a straight handle.
        ctx.beginPath()
        ctx.moveTo(0.62 * w, 0.46 * h)
        ctx.lineTo(0.62 * w, 0.24 * h)
        ctx.quadraticCurveTo(0.71 * w, 0.06 * h, 0.76 * w, 0.24 * h)
        ctx.lineTo(0.76 * w, 0.46 * h)
        ctx.closePath()
        ctx.fill()

        ctx.beginPath()
        ctx.moveTo(0.69 * w, 0.44 * h)
        ctx.lineTo(0.69 * w, 0.90 * h)
        ctx.stroke()
    }
}
