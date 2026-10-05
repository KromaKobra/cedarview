import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The window. What fills it follows login.phase:
//
//   welcome          WelcomeView — CedarView's own first screen
//   signingIn        SignInView — Microsoft's page in CedarView's chrome
//   everything else  the app: a large-title header, the banners, four tabs
//                    and a bottom bar
//
// The sign-in surface's Loader lives inside SignInView and stays instantiated
// in every phase: on the desktop the transport fetches through that page, so
// it must stay attached even while hidden.
ApplicationWindow {
    id: window
    visible: true
    width: 400
    height: 844
    title: "CedarView"
    color: theme.bg
    font.family: theme.ui

    Theme { id: theme }

    readonly property string phase: login.phase
    readonly property bool showingApp: phase === "checking" || phase === "signedIn"
                                       || phase === "needsSignIn" || phase === "preview"

    property int currentPage: 0
    // Which of DiningSections' pages is showing: plan, menu or hours.
    property int diningSection: 0
    property bool searchOpen: false

    readonly property var pageTitles: ["Today", "Chapel", "Dining", "Campus"]

    // Go wherever a tap points: a tab, a Dining section, and for a dish the
    // day and sitting to open the menu at.
    function navigateTo(tab, section, menuDay, menuMeal) {
        searchOpen = false
        if (tab === 2 && section >= 0)
            diningSection = section
        if (tab === 2 && section === 1) {
            dining.selectDay(menuDay)
            if (menuMeal && menuMeal.length > 0)
                dining.selectMeal(menuMeal)
        }
        currentPage = tab
    }

    function openSearch() {
        searchOpen = true
        searchPage.begin()
    }

    // For `cedarview --shoot`: put a named screen up. Not reachable from the
    // UI.
    function showScreen(name) {
        searchOpen = false
        chapelSheet.close()
        moreSheet.close()
        pages.contentItem.highlightMoveDuration = 0
        if (name === "today" || name === "today-night") {
            currentPage = 0
        } else if (name === "chapel" || name === "chapel-sheet") {
            currentPage = 1
            if (name === "chapel-sheet")
                chapelSheet.show({
                    who: chapel.nextSpeaker, subtitle: chapel.nextChapelTitle,
                    description: chapel.nextChapelDescription, dateText: chapel.nextChapelDateText.split(" · ")[0],
                    timeText: chapel.nextChapelTime, startsAt: chapel.nextChapelStartsAt,
                    livestream: chapel.nextChapelLivestream, youtubeId: chapel.nextChapelYoutubeId,
                    isToday: chapel.chapelToday
                })
        } else if (name === "plan" || name === "menu" || name === "hours") {
            diningSection = ["plan", "menu", "hours"].indexOf(name)
            currentPage = 2
        } else if (name === "campus") {
            currentPage = 3
        } else if (name === "search") {
            openSearch()
            searchPage.forceQuery("pizza")
        }
    }

    // ---- Android Back ------------------------------------------------------------
    // Delivered as a close request once no popup has taken it. Back closes a
    // sheet, search or the sign-in surface first; then goes to Today; only a
    // second Back with nothing in between closes the app. Desktop's window
    // close is left alone.
    property bool backWillClose: false
    onCurrentPageChanged: backWillClose = false
    onPhaseChanged: backWillClose = false

    onClosing: (close) => {
        if (Qt.platform.os !== "android")
            return
        if (searchOpen) {
            close.accepted = false
            searchOpen = false
            return
        }
        if (phase === "signingIn") {
            close.accepted = false
            login.cancelSignIn()
            return
        }
        if (showingApp && !backWillClose) {
            close.accepted = false
            // The edge-swipe Back gesture's first touch lands on the pages,
            // then Android takes the gesture over and Qt never sees that touch
            // end. A SwipeView left "pressed" ignores currentIndex, so the
            // header would say Today over a page that no longer moves, even for
            // the tab bar. Turning interactive off cancels the stale press.
            pages.interactive = false
            pages.interactive = true
            currentPage = 0
            // After the tab change, whose handler disarms.
            backWillClose = true
        }
    }

    // ---- The app -------------------------------------------------------------------
    Item {
        id: app
        anchors.fill: parent
        visible: window.showingApp

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // The large-title header: the date and how fresh things are, the
            // tab's name, search and More.
            Item {
                id: header
                Layout.fillWidth: true
                implicitHeight: headerRow.implicitHeight + 26 + header.SafeArea.margins.top

                RowLayout {
                    id: headerRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 12
                    anchors.leftMargin: 20 + header.SafeArea.margins.left
                    anchors.rightMargin: 16 + header.SafeArea.margins.right
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignBottom
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: {
                                const lead = window.currentPage === 1 && chapel.termLabel.length > 0
                                             ? chapel.termLabel : today.dateText
                                const stamp = window.phase === "preview" ? "sample data"
                                            : sync.offline ? "offline" : sync.lastUpdatedText
                                return lead + (stamp.length > 0 ? "<font color=\"" + theme.faint + "\"> · "
                                                                  + stamp + "</font>" : "")
                            }
                            textFormat: Text.StyledText
                            color: theme.muted
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: window.pageTitles[window.currentPage]
                            color: theme.text
                            font.family: theme.display
                            font.pixelSize: 34
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                    }

                    IconButton {
                        Layout.alignment: Qt.AlignBottom
                        glyph: "search"
                        text: "Search"
                        onClicked: window.openSearch()
                    }
                    IconButton {
                        Layout.alignment: Qt.AlignBottom
                        glyph: "more"
                        text: "More options"
                        onClicked: moreSheet.open()
                    }
                }
            }

            TopProgress {
                Layout.fillWidth: true
                running: sync.busy
            }

            // The banners: over the data, never instead of it.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: theme.pageMargin
                Layout.rightMargin: theme.pageMargin
                spacing: 8

                Banner {
                    visible: window.phase === "preview"
                    Layout.topMargin: 4
                    icon: "info"
                    title: "Sample data"
                    detail: "None of this is yours. Sign in to see your own."
                    actionText: "Sign in"
                    secondaryText: login.hasLoggedInBefore ? "Exit" : ""
                    onAction: login.startSignIn()
                    onSecondaryAction: login.exitPreview()
                }
                Banner {
                    visible: window.phase === "needsSignIn"
                    Layout.topMargin: 4
                    icon: "lock"
                    title: "Your session ended"
                    detail: "Sign in again to refresh your skips and meal plan."
                    actionText: "Sign in"
                    onAction: login.startSignIn()
                }
                Banner {
                    visible: sync.offline && window.phase !== "preview" && window.phase !== "needsSignIn"
                    Layout.topMargin: 4
                    icon: "wifiOff"
                    title: "You're offline"
                    detail: sync.savedAtText.length > 0 ? "Showing what was saved at " + sync.savedAtText
                                                        : "Nothing has been saved yet."
                    actionText: "Retry"
                    onAction: sync.refreshAll()
                }
                Item { Layout.preferredHeight: 4 }
            }

            SwipeView {
                id: pages
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.currentPage
                onCurrentIndexChanged: window.currentPage = currentIndex

                TodayView {
                    onNavigate: (tab, section, menuDay, menuMeal) => window.navigateTo(tab, section, menuDay, menuMeal)
                    onShowChapel: (details) => chapelSheet.show(details)
                }
                ChapelView {
                    onShowChapel: (details) => chapelSheet.show(details)
                }
                DiningSections {
                    section: window.diningSection
                    onSectionRequested: (index) => window.diningSection = index
                }
                CampusView {}
            }

            // The bottom bar.
            Rectangle {
                id: bottomBar
                Layout.fillWidth: true
                implicitHeight: 66 + bottomBar.SafeArea.margins.bottom
                color: theme.nav

                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: 1
                    color: theme.line
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.bottomMargin: bottomBar.SafeArea.margins.bottom
                    spacing: 0

                    Repeater {
                        model: [
                            { label: "Today", icon: "today" },
                            { label: "Chapel", icon: "chapel" },
                            { label: "Dining", icon: "dining" },
                            { label: "Campus", icon: "campus" }
                        ]
                        NavButton {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: modelData.label
                            glyph: modelData.icon
                            selected: window.currentPage === index
                            onClicked: window.currentPage = index
                        }
                    }
                }
            }
        }
    }

    WelcomeView {
        anchors.fill: parent
        visible: window.phase === "welcome"
    }

    SignInView {
        id: signIn
        anchors.fill: parent
        visible: login.surfaceVisible

        // Hand the surface to the transport and its address to the login
        // flow, once. The Loader may finish before or after this handler
        // exists, so both ends try.
        property bool wired: false
        function wire() {
            if (wired || !surface)
                return
            wired = true
            bridge.attachSurface(surface)
            surface.currentUrlChanged.connect(function () {
                login.onUrlChanged(surface.currentUrl)
            })
            surface.pageLoaded.connect(function (url) {
                login.onPageLoaded(url)
            })
        }
        onSurfaceChanged: wire()
        Component.onCompleted: wire()

        Connections {
            target: login
            function onNavigateRequested(url) {
                if (signIn.surface)
                    signIn.surface.navigate(url)
            }
        }
    }

    SearchView {
        id: searchPage
        anchors.fill: parent
        visible: window.searchOpen && window.showingApp
        onCloseRequested: window.searchOpen = false
        onNavigate: (tab, section, menuDay, menuMeal) => window.navigateTo(tab, section, menuDay, menuMeal)
    }

    ChapelSheet { id: chapelSheet }
    MoreSheet { id: moreSheet }
}
