import QtQuick

// CedarView's visual language, built on Cedarville's navy (#003A63) and gold
// (#FDB813). Surfaces are near-neutral with only a hint of cool blue so the two
// brand colours carry all the emphasis: blue for navigation and structure, gold
// for the figure that needs attention now. The hero cards are navy in both
// themes, so anything drawn on them uses the hero* tokens, never accent.
QtObject {
    readonly property bool light: (typeof settings !== "undefined") && settings.lightMode

    readonly property color background: light ? "#F3F4F6" : "#0B0D10"
    readonly property color backgroundTop: light ? "#F8F9FA" : "#0F1216"
    readonly property color ribbon: light ? "#F8F9FA" : "#0D1013"
    readonly property color nav: light ? "#FFFFFF" : "#111418"
    readonly property color card: light ? "#FFFFFF" : "#15181D"
    readonly property color cardAlt: light ? "#EEF1F5" : "#1B2027"
    readonly property color sheet: light ? "#FFFFFF" : "#1A1E24"
    readonly property color cardBorder: light ? "#DDE2E8" : "#252B33"
    readonly property color divider: light ? "#E3E7EC" : "#232830"
    readonly property color track: light ? "#DEE3EA" : "#272D36"
    // The semester meter: one blue, lit from above, over a recessed groove.
    readonly property color meterTop: light ? "#2475B3" : "#A9D2F5"
    readonly property color meterMid: light ? "#0B4F85" : "#7CB7EA"
    readonly property color meterBottom: light ? "#003A63" : "#5A9AD6"
    readonly property color meterGlow: light ? "#0B4F85" : "#3F80BD"
    readonly property color grooveTop: light ? "#D3DAE3" : "#1D2229"
    readonly property color grooveBottom: light ? "#E6EAF0" : "#2B323C"
    // A neutral surface raised out of a groove, such as a segmented thumb.
    readonly property color raisedTop: light ? "#FFFFFF" : "#3A424E"
    readonly property color raisedBottom: light ? "#F2F4F7" : "#2C333D"

    readonly property color text: light ? "#131820" : "#F2F4F7"
    readonly property color muted: light ? "#5B6570" : "#A3ACB7"
    readonly property color faint: light ? "#7F8893" : "#737C87"
    readonly property color textOnAccent: light ? "#FFFFFF" : "#0D1B2A"
    readonly property color textOnCedar: light ? "#FFFFFF" : "#0D1B2A"
    readonly property color textOnDark: "#F5F8FB"

    // Gold is darkened in the light theme: #FDB813 on white is under 2:1.
    readonly property color accent: light ? "#9A6700" : "#FAC03D"
    readonly property color accentDeep: light ? "#7A5200" : "#E0A21C"
    readonly property color accentSoft: light ? "#FBF0D6" : "#2B2413"
    // "cedar" is the primary brand blue; the name predates the palette.
    readonly property color cedar: light ? "#0B4F85" : "#7CB7EA"
    readonly property color cedarDeep: light ? "#003A63" : "#3F80BD"
    readonly property color cedarSoft: light ? "#E4EDF6" : "#17273A"
    readonly property color violet: light ? "#5E4FB8" : "#B3A6F2"
    readonly property color violetSoft: light ? "#ECE9FA" : "#262340"
    // A raised violet surface, lit from above like the meter fill.
    readonly property color violetTop: light ? "#7466D0" : "#C9BEF8"
    readonly property color violetBottom: light ? "#4B3DA3" : "#9C8CE6"
    readonly property color textOnViolet: light ? "#FFFFFF" : "#18123A"
    readonly property color danger: light ? "#B8403A" : "#FF8D83"
    readonly property color dangerSoft: light ? "#FBE8E5" : "#3A1D1B"

    readonly property color heroStart: light ? "#0B4A7C" : "#0E3F68"
    readonly property color heroEnd: light ? "#002D4F" : "#0A2641"
    readonly property color heroAccent: "#FDB813"
    readonly property color heroAccentSoft: Qt.rgba(0.99, 0.72, 0.07, 0.14)
    readonly property color heroDanger: "#FF8D83"
    readonly property color mealStart: light ? "#FBF0D6" : "#2A2314"
    readonly property color mealEnd: light ? "#EAF0F7" : "#15202D"

    readonly property color pressed: light ? Qt.rgba(0, 0, 0, 0.055)
                                           : Qt.rgba(1, 1, 1, 0.075)
    readonly property color pressedStrong: light ? Qt.rgba(0, 0, 0, 0.10)
                                                 : Qt.rgba(1, 1, 1, 0.13)
    readonly property color hairline: light ? Qt.rgba(0, 0, 0, 0.07)
                                            : Qt.rgba(1, 1, 1, 0.07)
    readonly property color scrim: light ? Qt.rgba(0.02, 0.07, 0.12, 0.36)
                                         : Qt.rgba(0, 0, 0, 0.68)

    readonly property int pageMargin: 18
    readonly property int cardPadding: 20
    readonly property int cardRadius: 24
    readonly property int gap: 12
}
