// A horizontal meter: track, gradient fill, nothing else.
//
// Lifted out of the chapel card on the summary screen when a second bar — the
// semester countdown — appeared underneath it. Two bars stacked a few hundred
// pixels apart have to be identical or the difference reads as meaning
// something, and two copies of a gradient plus an easing curve do not stay
// identical for long.
//
// Deliberately unlabelled, and deliberately not a Controls ProgressBar. What a
// bar adds over the figure printed above it is the sense of how far through you
// are without reading anything, and ProgressBar's Basic style brings its own
// light colours to a screen that has already chosen its own.
//
// `fraction` is always **remaining**, never elapsed — a full bar means plenty
// left, on both of them. Getting that backwards on one of two adjacent bars is
// the single worst thing this component could do.

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: meter

    //: 0.0–1.0. Clamped here as well as in the viewmodels, because a bar that
    //: overflows its own track looks broken where a full one just looks full.
    property real fraction: 0.0

    Theme { id: theme }

    Layout.fillWidth: true
    height: 8
    radius: height / 2
    color: theme.track

    Rectangle {
        // Floored at the bar's own height so a near-zero fraction renders as a
        // round dot rather than a one-pixel sliver of orange.
        width: Math.max(parent.height,
                        parent.width * Math.max(0.0, Math.min(1.0, meter.fraction)))
        height: parent.height
        radius: parent.radius

        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: theme.accentDeep }
            GradientStop { position: 1.0; color: theme.accent }
        }

        // Every fraction starts at 0 and jumps to its real value when the fetch
        // lands (or, for the semester bar, on the first frame). Animating that
        // makes it read as the bar filling rather than as the screen twitching.
        Behavior on width {
            NumberAnimation { duration: 320; easing.type: Easing.OutCubic }
        }
    }
}
