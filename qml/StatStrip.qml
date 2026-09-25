// The headline figures that sit above a tab's detail, as one slim row.
//
// Deliberately quiet: the list beneath is what the tab is for, so this only
// has to answer "how much is left" at a glance. The first figure leads and is
// drawn a little larger; the rest follow in equal columns split by hairlines.
//
// `stats` is a list of { value, label, tone }, where tone is optional and
// colours the value.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Card {
    id: strip

    property var stats: []

    Theme { id: theme }

    padding: 14

    RowLayout {
        Layout.fillWidth: true
        spacing: 0

        Repeater {
            model: strip.stats

            RowLayout {
                required property var modelData
                required property int index

                Layout.fillWidth: true
                // Equal columns: a fillWidth RowLayout divides space in
                // proportion to preferredWidth.
                Layout.preferredWidth: 1
                // Values share a baseline even when one label wraps.
                Layout.alignment: Qt.AlignBaseline
                baselineOffset: figure.baselineOffset
                spacing: 12

                Rectangle {
                    visible: index > 0
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: theme.divider
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 1

                    Label {
                        id: figure
                        Layout.fillWidth: true
                        text: modelData.value
                        color: modelData.tone !== undefined ? modelData.tone : theme.text
                        font.pixelSize: index === 0 ? 22 : 16
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: modelData.label
                        color: theme.faint
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
