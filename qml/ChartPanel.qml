// ChartPanel.qml - 그래프 하나 (제목·범례 + GraphsView + 상태 띠·커서·마우스 조작)
//
// def (PlotBridge.panels의 한 항목)
//   { id, title, unit, decimals, satKey, series: [{ key, name, color, width }] }
//
// 선(LineSeries)은 def.series 개수만큼 시작할 때 만들어 GraphsView에 넣고,
// bridge.registerSeries로 C++에 알린다. 점 목록은 C++이 replace()로 넣는다.
// 가로·세로 축 범위도 C++이 정한다 (registerPanel).
//
// 겹친 층(plot)은 GraphsView.plotArea(축·여백을 뺀 그림 영역, Qt 6.9~)에 맞춰
// 상태 띠·커서 선을 그리고 마우스를 받는다. 시각 ↔ 화면 x는 bridge.xMin~xMax로 환산한다.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtGraphs

Item {
    id: panel

    required property QtObject bridge
    required property var def

    readonly property real viewSpan: Math.max(1e-9, panel.bridge.xMax - panel.bridge.xMin)
    property real dragLastX: 0

    /** 범례 값 표시 (없으면 –) */
    function fmt(v) {
        if (v === undefined || v === null || isNaN(v))
            return "–"
        return v.toFixed(panel.def.decimals)
    }
    /** 그림 영역 x → 시각 [s] */
    function timeAtX(px) {
        return panel.bridge.xMin + px / Math.max(1, plot.width) * panel.viewSpan
    }
    /** 시각 [s] → 그림 영역 x */
    function xAtTime(t) {
        return (t - panel.bridge.xMin) / panel.viewSpan * plot.width
    }

    Component {
        id: lineComp
        LineSeries {
            width: 1.5
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 제목과 범례 (값 = 커서 시각의 값, 실시간이면 마지막 값) ----
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            spacing: 10

            Text {
                text: panel.def.title + " [" + panel.def.unit + "]"
                font.pixelSize: 12
                font.bold: true
                color: "#24292f"
            }
            Repeater {
                model: panel.def.series
                delegate: Row {
                    id: entry
                    required property var modelData
                    spacing: 4
                    Rectangle {
                        width: 14
                        height: 3
                        radius: 1
                        color: entry.modelData.color
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        font.pixelSize: 12
                        font.family: "Consolas"
                        color: "#24292f"
                        text: {
                            // revision·cursorTime이 바뀌면 다시 계산되도록 읽어 둔다
                            const rev = panel.bridge.revision
                            const cur = panel.bridge.cursorTime
                            return entry.modelData.name + " "
                                   + panel.fmt(panel.bridge.valueAt(entry.modelData.key))
                        }
                    }
                }
            }
            Text {
                visible: {
                    const rev = panel.bridge.revision
                    const cur = panel.bridge.cursorTime
                    return panel.def.satKey !== ""
                           && panel.bridge.valueAt(panel.def.satKey) >= 0.5
                }
                text: "포화"
                font.pixelSize: 12
                font.bold: true
                color: "#cf222e"
            }
        }

        // ---- 그래프 + 겹친 층 ----
        Item {
            id: chartBox
            Layout.fillWidth: true
            Layout.fillHeight: true

            GraphsView {
                id: graph
                anchors.fill: parent
                marginLeft: 4
                marginRight: 10
                marginTop: 4
                marginBottom: 2

                theme: GraphsTheme {
                    colorScheme: GraphsTheme.ColorScheme.Light
                    backgroundColor: "#ffffff"
                    plotAreaBackgroundColor: "#ffffff"
                    grid.mainColor: "#d0d7de"
                    grid.subColor: "#eaeef2"
                    labelTextColor: "#57606a"
                    labelBackgroundVisible: false
                    axisXLabelFont.pixelSize: 10
                    axisYLabelFont.pixelSize: 10
                }

                axisX: ValueAxis {
                    id: axX
                    min: 0
                    max: 20
                }
                axisY: ValueAxis {
                    id: axY
                    min: -1
                    max: 1
                }
            }

            Item {
                id: plot
                x: graph.plotArea.x
                y: graph.plotArea.y
                width: graph.plotArea.width
                height: graph.plotArea.height
                clip: true

                // 상태 띠 (그림 영역 위쪽 4 px)
                Repeater {
                    model: panel.bridge.stateSpans
                    delegate: Rectangle {
                        required property var modelData
                        x: panel.xAtTime(modelData.t0)
                        width: Math.max(1, panel.xAtTime(modelData.t1) - x)
                        y: 0
                        height: 4
                        color: modelData.color
                    }
                }

                // 커서 선 (일시정지 중)
                Rectangle {
                    visible: panel.bridge.paused && !isNaN(panel.bridge.cursorTime)
                    x: isNaN(panel.bridge.cursorTime) ? 0 : panel.xAtTime(panel.bridge.cursorTime)
                    width: 1
                    height: parent.height
                    color: "#57606a"
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton

                    // 클릭: 멈추고 그 시각에 커서
                    onPressed: (mouse) => {
                        panel.dragLastX = mouse.x
                        if (!panel.bridge.paused)
                            panel.bridge.paused = true
                        panel.bridge.cursorTime = panel.timeAtX(mouse.x)
                    }
                    // 멈춘 동안: 누른 채 움직이면 가로 이동, 그냥 움직이면 커서
                    onPositionChanged: (mouse) => {
                        if (!panel.bridge.paused)
                            return
                        if (mouseArea.pressed) {
                            const dt = -(mouse.x - panel.dragLastX) / Math.max(1, plot.width)
                                       * panel.viewSpan
                            panel.dragLastX = mouse.x
                            panel.bridge.panBy(dt)
                        } else {
                            panel.bridge.cursorTime = panel.timeAtX(mouse.x)
                        }
                    }
                    onDoubleClicked: panel.bridge.fitAll()
                    // 휠: 마우스 위치를 중심으로 확대(위)·축소(아래). 실시간이면 먼저 멈춘다
                    onWheel: (wheel) => {
                        if (!panel.bridge.paused)
                            panel.bridge.paused = true
                        panel.bridge.zoomAt(panel.timeAtX(wheel.x),
                                            wheel.angleDelta.y > 0 ? 0.8 : 1.25)
                        wheel.accepted = true
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        for (let i = 0; i < panel.def.series.length; ++i) {
            const d = panel.def.series[i]
            const s = lineComp.createObject(graph, { "name": d.name, "color": d.color, "width": d.width })
            graph.addSeries(s)
            panel.bridge.registerSeries(panel.def.id, d.key, s)
        }
        panel.bridge.registerPanel(panel.def.id, axX, axY)
    }
}
