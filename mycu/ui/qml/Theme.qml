// The palette and the spacing scale, in one place.
//
// A plain QtObject rather than a `pragma Singleton`, and instantiated in each
// file that wants it (`Theme { id: theme }`). A singleton would need a qmldir
// and a real QML module directory; this is half a dozen colour strings, it
// costs nothing to have several of, and same-directory components resolve
// without any import path at all — which is one less thing to go wrong in the
// APK, where the QML files are listed by hand in pysidedeploy.spec.
//
// Dark only, deliberately. The app is read at 7am on a phone in a dim dorm
// room, and every number on it is a number you want to glance at, not study.

import QtQuick

QtObject {
    // ---- Surfaces -------------------------------------------------------
    readonly property color background:  "#0A0A0C"
    readonly property color ribbon:      "#0E0E11"
    readonly property color card:        "#161619"
    readonly property color cardBorder:  "#232328"
    readonly property color divider:     "#26262C"
    readonly property color track:       "#2A2A31"

    // ---- Text -----------------------------------------------------------
    readonly property color text:        "#F3F3F5"
    readonly property color muted:       "#8E8E98"
    readonly property color faint:       "#60606B"

    // ---- Accents --------------------------------------------------------
    // Orange is the app's colour: the logo, the figure that matters most on
    // each card, and the selected tab. Used sparingly enough that it always
    // means "look here".
    readonly property color accent:      "#F0A44A"
    readonly property color accentDeep:  "#E07B2E"

    // Violet marks the *other* of a pair — the temporary flex balance against
    // the permanent one, the day badge against the chapel title. Never used
    // for state, so it never has to compete with the orange.
    readonly property color violet:      "#98A2F8"
    readonly property color violetSoft:  "#23264A"

    readonly property color danger:      "#F2726B"
    readonly property color dangerSoft:  "#2A1618"

    // ---- Metrics --------------------------------------------------------
    readonly property int pageMargin:    14
    readonly property int cardPadding:   18
    readonly property int cardRadius:    18
    readonly property int gap:           10
}
