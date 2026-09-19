// The palette and the spacing scale, in one place.
//
// A plain QtObject rather than a `pragma Singleton`, and instantiated in each
// file that wants it (`Theme { id: theme }`). A singleton would need a qmldir
// and a real QML module directory; this is a couple of dozen colour strings, it
// costs nothing to have several of, and same-directory components resolve
// without any import path at all — which is one less thing to go wrong in the
// APK, where the QML files are listed by hand in pysidedeploy.spec.
//
// ---- Two palettes ---------------------------------------------------------
//
// Dark is the default and still the one the app was designed around: it is read
// at 7am on a phone in a dim dorm room, and every number on it is a number you
// want to glance at, not study. Light exists because the same phone gets read
// outdoors.
//
// Being instantiated eight times is exactly the problem a theme switch has to
// solve — eight objects have to give the same answer at the same moment. They
// do it by all binding to one context property, `settings.lightMode`
// (mycu/ui/settings.py), so the switch is a single property change that every
// instance sees. The `typeof` guard means a Theme loaded into a context without
// that property still resolves, dark, instead of erroring out.
//
// Every colour on a screen a user sees is in this file. If you find yourself
// typing a hex string or a Qt.rgba() anywhere else, it needs a token here
// instead — the nine that had escaped are what made the light theme a morning's
// work rather than an hour's. (WebSurfaceStub.qml is the one exception, and it
// only exists under --demo.)

import QtQuick

QtObject {
    readonly property bool light: (typeof settings !== "undefined") && settings.lightMode

    // ---- Surfaces -------------------------------------------------------
    readonly property color background:  light ? "#F6F5F3" : "#0A0A0C"
    readonly property color ribbon:      light ? "#FFFFFF" : "#0E0E11"
    readonly property color card:        light ? "#FFFFFF" : "#161619"
    readonly property color cardBorder:  light ? "#E4E2DE" : "#232328"
    readonly property color divider:     light ? "#E4E2DE" : "#26262C"
    readonly property color track:       light ? "#E7E5E1" : "#2A2A31"

    //: Menus and modal sheets, which sit *above* a card and need to separate
    //: from it. In the dark theme that means lifting off the card colour; in
    //: the light one it means staying white while the page behind goes warm.
    readonly property color sheet:       light ? "#FFFFFF" : "#1A1A1F"

    // ---- Text -----------------------------------------------------------
    readonly property color text:        light ? "#14141A" : "#F3F3F5"
    readonly property color muted:       light ? "#63636E" : "#8E8E98"
    readonly property color faint:       light ? "#8E8E98" : "#60606B"

    // ---- Accents --------------------------------------------------------
    // Orange is the app's colour: the figure that matters most on each card,
    // and the selected tab. Used sparingly enough that it always means "look
    // here". The light values are darker rather than the same hex — #F0A44A on
    // white is about 1.9:1 against the background, which is not a colour you
    // can put a number in.
    readonly property color accent:      light ? "#C7701A" : "#F0A44A"
    readonly property color accentDeep:  light ? "#A65712" : "#E07B2E"

    //: The selected-tab pill: the accent at low alpha, mixed per theme rather
    //: than derived, because alpha over black and alpha over white do not meet
    //: in the middle.
    readonly property color accentSoft:  light ? Qt.rgba(0.78, 0.44, 0.10, 0.12)
                                               : Qt.rgba(0.94, 0.64, 0.29, 0.14)

    // Violet marks the *other* of a pair — the temporary flex balance against
    // the permanent one, the day badge against the chapel title. Never used
    // for state, so it never has to compete with the orange.
    readonly property color violet:      light ? "#4F57C4" : "#98A2F8"
    readonly property color violetSoft:  light ? "#E7E9FD" : "#23264A"

    readonly property color danger:      light ? "#C0392B" : "#F2726B"
    readonly property color dangerSoft:  light ? "#FDECEA" : "#2A1618"

    // ---- Washes ---------------------------------------------------------
    // Pressed states, hairlines and the modal scrim. All of these used to be
    // written inline as Qt.rgba(1, 1, 1, …) — white at low alpha, which is
    // invisible on a white card.
    readonly property color pressed:       light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.08)
    readonly property color pressedStrong: light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.12)
    readonly property color hairline:      light ? Qt.rgba(0, 0, 0, 0.07) : Qt.rgba(1, 1, 1, 0.06)

    //: Behind a modal. Lighter in the light theme: 60% black over an off-white
    //: page reads as a different screen rather than as a covered one.
    readonly property color scrim:         light ? Qt.rgba(0, 0, 0, 0.35) : Qt.rgba(0, 0, 0, 0.60)

    // ---- Metrics --------------------------------------------------------
    readonly property int pageMargin:    14
    readonly property int cardPadding:   18
    readonly property int cardRadius:    18
    readonly property int gap:           10
}
