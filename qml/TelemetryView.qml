// TelemetryView.qml - 그래프 탭의 QML 화면 (Qt Graphs)
//
// 위 : 상태 색 범례, 조작 안내
// 아래: 그래프 6개 (2열 × 3행). 무엇을 그릴지는 C++ PlotBridge.panels가 정한다.
//
// bridge(PlotBridge)는 C++ TelemetryView가 setInitialProperties로 넘겨준다.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    required property QtObject bridge

    color: "#ffffff"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        // ---- 상태 범례와 조작 안내 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "상태"
                font.pixelSize: 12
                color: "#57606a"
            }
            Repeater {
                model: root.bridge.stateLegend
                delegate: Row {
                    id: legendItem
                    required property var modelData
                    spacing: 4
                    Rectangle {
                        width: 14
                        height: 6
                        radius: 1
                        color: legendItem.modelData.color
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: legendItem.modelData.name
                        font.pixelSize: 12
                        color: "#24292f"
                    }
                }
            }
            Item {
                Layout.fillWidth: true
            }
            Text {
                text: root.bridge.paused
                      ? "일시정지 · 휠: 확대/축소 · 끌기: 이동 · 두 번 클릭: 이동 전체"
                      : "실시간 · 그래프를 클릭하면 멈춥니다"
                font.pixelSize: 12
                color: root.bridge.paused ? "#9a6700" : "#57606a"
            }
        }

        // ---- 그래프 6개 ----
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            rowSpacing: 6
            columnSpacing: 12
            uniformCellWidths: true
            uniformCellHeights: true

            Repeater {
                model: root.bridge.panels
                delegate: ChartPanel {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 110
                    bridge: root.bridge
                    def: modelData
                }
            }
        }
    }
}
