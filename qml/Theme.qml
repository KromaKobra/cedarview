import QtQuick

// CedarView's visual language, built on Cedarville's navy and gold, as the
// v0.4 canvas set it out. Surfaces are near-neutral so the two brand colours
// carry the emphasis: navy for the hero cards and structure, gold for the
// figure that matters now, cedar blue for navigation, violet for money. Each
// accent comes as a pair — the colour and a soft fill of the same hue — plus
// a light-to-deep pair (…A, …B) for anything that should look lit from above.
// One hue per element: depth comes from shading within it, never from a
// second colour.
//
// The hero cards are navy in both themes, so anything drawn on them uses the
// on-navy tokens (navyText, goldHero, heroInset, …), never the page's.
//
// A plain QtObject, instantiated once per file — not a singleton. Every copy
// binds `light` to the `settings` context property, so they all change
// together; that is the one thing that has to be shared.
QtObject {
    readonly property bool light: (typeof settings !== "undefined") && settings.lightMode

    // ---- The page
    readonly property color bg: light ? "#F2F4F7" : "#0A0C0F"
    readonly property color surface: light ? "#FFFFFF" : "#14171C"
    readonly property color surface2: light ? "#EDF1F5" : "#1B2027"
    readonly property color raised: light ? "#FFFFFF" : "#2A313B"
    readonly property color line: light ? "#DEE3EA" : "#262C35"
    readonly property color text: light ? "#121821" : "#F2F4F7"
    readonly property color muted: light ? "#55606C" : "#A9B2BD"
    readonly property color faint: light ? "#68717D" : "#8A939E"
    readonly property color nav: light ? Qt.rgba(1, 1, 1, 0.94) : Qt.rgba(0.051, 0.063, 0.078, 0.92)
    readonly property color scrim: light ? Qt.rgba(0.02, 0.07, 0.12, 0.42) : Qt.rgba(0, 0, 0, 0.62)
    // Under a card in the light theme; the dark one lifts cards with a top
    // highlight instead (cardHighlight).
    readonly property color cardShadow: Qt.rgba(0.063, 0.094, 0.157, light ? 0.07 : 0)
    readonly property color cardHighlight: light ? "transparent" : Qt.rgba(1, 1, 1, 0.04)

    // ---- Accents. Gold is darkened in the light theme: #FDB813 on white is
    // under 2:1.
    readonly property color gold: light ? "#8F5F00" : "#FAC03D"
    readonly property color goldSoft: light ? "#FBF0D6" : "#2A2312"
    readonly property color goldA: light ? "#E0A52A" : "#FFD566"
    readonly property color goldB: light ? "#9A6700" : "#E9A510"
    // "cedar" is the primary brand blue; the name predates the palette.
    readonly property color cedar: light ? "#0B4F85" : "#7CB7EA"
    readonly property color cedarSoft: light ? "#E3EDF7" : "#152639"
    readonly property color meterA: light ? "#2C7DBD" : "#A9D2F5"
    readonly property color meterB: light ? "#0B4F85" : "#5A9AD6"
    readonly property color violet: light ? "#5A4BB5" : "#B8ABF5"
    readonly property color violetSoft: light ? "#ECE9FA" : "#24203C"
    readonly property color violetA: light ? "#7F72D6" : "#D3CBFB"
    readonly property color violetB: light ? "#4B3DA3" : "#9C8CE6"
    readonly property color danger: light ? "#B23A34" : "#FF8D83"
    readonly property color dangerSoft: light ? "#FBE8E5" : "#3A1D1B"
    // Text on a filled gold or cedar button.
    readonly property color accentText: light ? "#FFFFFF" : "#0A0C0F"

    // ---- The navy hero, the same in both themes
    readonly property color navyHi: "#1D6099"
    readonly property color navy: "#0D406D"
    readonly property color navyLo: "#062240"
    readonly property color navyText: "#F5F8FB"
    readonly property color navyMuted: Qt.rgba(0.961, 0.973, 0.984, 0.76)
    readonly property color goldHero: "#FDB813"
    readonly property color heroPipA: "#FFD15C"
    readonly property color heroPipB: "#F2A900"
    readonly property color heroPipOff: Qt.rgba(1, 1, 1, 0.16)
    readonly property color heroSky: "#A9D2F5"
    readonly property color heroDanger: "#FFA79F"
    readonly property color heroInset: Qt.rgba(0, 0.04, 0.1, 0.28)
    readonly property color heroLine: Qt.rgba(1, 1, 1, 0.12)
    readonly property color heroChip: Qt.rgba(1, 1, 1, 0.10)
    readonly property color heroSkeleton: Qt.rgba(1, 1, 1, 0.12)
    // The flex-pace bar, a light violet that reads on navy.
    readonly property color paceA: "#DDD6FC"
    readonly property color paceB: "#A797EE"
    // The gold button: lit from above.
    readonly property color buttonA: "#FFCB45"
    readonly property color buttonB: "#F2A900"
    readonly property color buttonText: "#17110A"

    readonly property color pressed: light ? Qt.rgba(0, 0, 0, 0.05) : Qt.rgba(1, 1, 1, 0.06)

    // ---- Type. Registered by main.cpp from qml/fonts/ before QML loads.
    // Bricolage for display figures and titles, Instrument Sans for the rest.
    readonly property string display: "Bricolage Grotesque"
    readonly property string ui: "Instrument Sans"

    // ---- Metrics
    readonly property int pageMargin: 16
    readonly property int cardRadius: 24
    readonly property int heroRadius: 26
    readonly property int gap: 12
}
