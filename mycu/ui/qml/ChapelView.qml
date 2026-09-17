// Chapel attendance. Binds to the `chapel` context property
// (mycu.ui.viewmodels.chapel.ChapelViewModel).
//
// Three states, and the distinction between them matters:
//   - error      something went wrong and the user should know what
//   - empty      loaded fine, there is genuinely nothing to show
//   - loaded     rows
//
// An unloaded view deliberately does NOT render "0 skips" — showing a confident
// zero before the data arrives would be the app lying in the reassuring
// direction, which is the worse direction to lie in.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Summary header -------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: summary.implicitHeight + 32
            color: Qt.rgba(0, 0, 0, 0.04)
            visible: chapel.loaded

            ColumnLayout {
                id: summary
                anchors.centerIn: parent
                width: parent.width - 32
                spacing: 4

                Label {
                    text: chapel.term.length > 0 ? chapel.term : "This term"
                    font.pixelSize: 13
                    opacity: 0.7
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    // -1 means Cedarville didn't report the figure. Show what we
                    // have rather than inventing a denominator or a zero.
                    text: chapel.allowed >= 0 && chapel.used >= 0
                          ? chapel.used + " of " + chapel.allowed + " skips used"
                          : (chapel.used >= 0
                             ? chapel.used + (chapel.used === 1 ? " skip used" : " skips used")
                             : "—")
                    font.pixelSize: 26
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    visible: chapel.remaining >= 0
                    text: chapel.remaining + " remaining"
                    font.pixelSize: 14
                    opacity: 0.75
                    color: chapel.remaining <= 1 ? "#b3261e" : palette.text
                    Layout.alignment: Qt.AlignHCenter
                }

                // The only place the app can explain why the total is 18 rather
                // than the 17 everyone expects.
                Label {
                    visible: text.length > 0
                    text: chapel.allowanceText
                    font.pixelSize: 11
                    opacity: 0.5
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    visible: !chapel.inGoodStanding
                    text: "Not in good standing"
                    color: "#b3261e"
                    font.pixelSize: 13
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // ---- Next speaker ---------------------------------------------------
        // Different service entirely (mediaserve.cedarville.edu, no auth), but
        // this is where a reader expects it. Hidden outright when there is no
        // next chapel — over the summer there genuinely isn't one, and "TBA"
        // would be a claim we can't support.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: nextRow.implicitHeight + 24
            visible: chapel.nextSpeaker.length > 0
            color: Qt.rgba(0, 0, 0, 0.02)

            ColumnLayout {
                id: nextRow
                anchors.centerIn: parent
                width: parent.width - 32
                spacing: 2

                Label {
                    text: "NEXT CHAPEL" + (chapel.nextChapelWhen.length > 0
                                           ? " · " + chapel.nextChapelWhen : "")
                    font.pixelSize: 11
                    font.bold: true
                    opacity: 0.55
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: chapel.nextSpeaker
                    font.pixelSize: 17
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }

                Label {
                    visible: text.length > 0
                    text: chapel.nextChapelTitle
                    font.pixelSize: 12
                    opacity: 0.6
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Qt.rgba(0, 0, 0, 0.08)
            }
        }

        // ---- Error ----------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: errorLabel.implicitHeight + 24
            visible: chapel.error.length > 0
            color: "#fdecea"

            Label {
                id: errorLabel
                anchors.centerIn: parent
                width: parent.width - 32
                wrapMode: Text.Wrap
                color: "#7f1d1d"
                font.pixelSize: 13
                text: chapel.error
            }
        }

        // ---- Rows -----------------------------------------------------------
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: chapel.records
            spacing: 0

            delegate: ItemDelegate {
                width: list.width
                height: 64
                enabled: false                   // read-only

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12

                    // Red for a skip spent, green for one given back. A manual
                    // adjustment must not read as another absence.
                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        Layout.alignment: Qt.AlignVCenter
                        color: model.isSkip ? "#b3261e" : "#137333"
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: model.whenText
                            font.pixelSize: 15
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Label {
                            visible: text.length > 0
                            text: model.reason.length > 0 ? model.reason : model.entryType
                            font.pixelSize: 12
                            opacity: 0.65
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    Label {
                        text: (model.count > 0 ? "+" : "") + model.count
                        font.pixelSize: 15
                        font.bold: true
                        opacity: 0.8
                        color: model.isSkip ? "#b3261e" : "#137333"
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Qt.rgba(0, 0, 0, 0.08)
                }
            }

            // ---- Empty / first-run state ------------------------------------
            Label {
                anchors.centerIn: parent
                width: parent.width - 64
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                opacity: 0.6
                visible: list.count === 0 && !chapel.busy
                text: chapel.loaded
                      ? "No skips recorded this term."
                      : (chapel.error.length > 0
                         ? ""
                         : "Pull to refresh, or tap ↻ above.")
            }

            // Pull-to-refresh. Works with a mouse on desktop too, which makes
            // it testable without a phone.
            BusyIndicator {
                running: chapel.busy
                visible: running && list.count === 0
                anchors.centerIn: parent
            }

            onDragEnded: {
                if (contentY < -80 && !chapel.busy) {
                    chapel.refresh()
                }
            }
        }
    }
}
