/**
 * @file    plot_bridge.hpp
 * @brief   그래프 화면(QML, Qt Graphs)과 데이터 버퍼(C++) 사이의 연결
 *
 * ---------------------------------------------------------------------------
 * [역할 나누기]
 *   - QML (qml/TelemetryView.qml, ChartPanel.qml)
 *       그래프 6개의 배치, 선·축·범례 모양, 마우스 조작(커서·확대·이동)
 *       시작할 때 자기가 만든 선(LineSeries)과 축(ValueAxis)을 registerSeries/registerPanel로 알려 준다
 *   - PlotBridge (이 클래스)
 *       무엇을 그릴지(panels 정의), 보이는 시간 구간(xMin~xMax), 세로축 범위,
 *       선마다 점 목록을 버퍼에서 꺼내 넣기(replace), 커서 값, 상태 띠
 *   - TelemetryBuffer (core): 데이터 보관·조회 (단위 시험 대상)
 *
 *   그래프 정의(panels)를 C++ 한곳에 두어, 채널 키 오타가 생기면 registerSeries에서 바로 로그로 드러난다.
 *
 * [갱신]
 *   telemetry는 20 Hz로 들어오지만 그래프는 refresh 타이머(50 ms)에서 바뀐 것이 있을 때만 다시 그린다.
 *   선마다 replace() 한 번으로 점 목록 전체를 바꾼다 (점 하나씩 append하면 신호가 점마다 나감).
 *
 * [보기 방식]
 *   - 실시간 : 마지막 windowSeconds초 (0이면 이동 전체). 이동이 창보다 짧으면 0초부터 왼쪽 정렬
 *   - 일시정지: 보이는 구간 고정, 휠 확대·끌기 이동·두 번 클릭 전체 보기, 커서 위치 값 표시
 *   세로축은 보이는 값에 맞추되, 실시간에서는 넓힐 때는 바로, 좁힐 때는 천천히(출렁임 방지)
 * ---------------------------------------------------------------------------
 */
#ifndef AMR_TUNER_UI_PLOT_BRIDGE_HPP
#define AMR_TUNER_UI_PLOT_BRIDGE_HPP

#include <QColor>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtGraphs/QValueAxis>
#include <QtGraphs/QXYSeries>

#include "core/telemetry_buffer.hpp"

namespace amr
{

class PlotBridge : public QObject
{
  Q_OBJECT
  Q_PROPERTY(double xMin READ xMin NOTIFY viewChanged)
  Q_PROPERTY(double xMax READ xMax NOTIFY viewChanged)
  Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
  Q_PROPERTY(double cursorTime READ cursorTime WRITE setCursorTime NOTIFY cursorChanged)
  Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
  Q_PROPERTY(QVariantList stateSpans READ stateSpans NOTIFY revisionChanged)
  Q_PROPERTY(QVariantList panels READ panels CONSTANT)
  Q_PROPERTY(QVariantList stateLegend READ stateLegend CONSTANT)

public:
  explicit PlotBridge(TelemetryBuffer & buffer, QObject * parent = nullptr);
  ~PlotBridge() override;

  double xMin() const {return xMin_;}
  double xMax() const {return xMax_;}
  bool paused() const {return paused_;}
  double cursorTime() const {return cursorTime_;}
  int revision() const {return revision_;}
  QVariantList stateSpans() const {return stateSpans_;}

  /** 그래프 정의: [{id, title, unit, decimals, satKey, series:[{key, name, color, width}]}] */
  QVariantList panels() const;
  /** 상태 색 범례: [{state, name, color}] */
  QVariantList stateLegend() const;

  /** 상태 번호 → 띠 색 */
  static QColor stateColor(int state);

  void setPaused(bool on);
  void setCursorTime(double t);

  /** 실시간 보기 창 [s]. 0 = 이동 전체 */
  void setWindowSeconds(double s);
  double windowSeconds() const {return windowS_;}

  /** 새 데이터가 들어옴 (다음 refresh에서 다시 그림) */
  void markDirty() {dirty_ = true;}

  /** 새 이동·지우기: 세로축 기억을 버리고 커서를 지운다 */
  void resetView();

  /* ---- QML에서 부르는 함수 ---- */

  /** QML이 만든 선 등록. panelId 그래프의 key 채널을 그린다 */
  Q_INVOKABLE void registerSeries(const QString & panelId, const QString & key, QObject * series);
  /** QML이 만든 그래프의 가로·세로 축 등록 */
  Q_INVOKABLE void registerPanel(const QString & panelId, QObject * axisX, QObject * axisY);
  /** 범례 표시 값: 일시정지 중이고 커서가 있으면 커서 시각, 아니면 마지막 값. 없으면 NaN */
  Q_INVOKABLE double valueAt(const QString & key) const;
  /** 일시정지 중 확대·축소 (factor < 1이면 확대), t를 중심으로 */
  Q_INVOKABLE void zoomAt(double t, double factor);
  /** 일시정지 중 가로 이동 [s] */
  Q_INVOKABLE void panBy(double dt);
  /** 이동 전체 보기 (일시정지 상태로) */
  Q_INVOKABLE void fitAll();

signals:
  void viewChanged();
  void pausedChanged(bool paused);
  void cursorChanged();
  void revisionChanged();

private:
  struct SeriesRef
  {
    QString key;
    Channel channel{Channel::CmdV};
    QPointer<QXYSeries> series;
  };
  struct PanelRef
  {
    QList<SeriesRef> series;
    QPointer<QValueAxis> axisX;
    QPointer<QValueAxis> axisY;
    double lo{0.0};          ///< 지금 세로축 범위
    double hi{1.0};
    bool hasRange{false};
  };

  /** 50 ms 주기: 바뀐 것이 있으면 다시 그림 */
  void refresh();
  /** 실시간 보기의 가로 구간 계산 */
  void updateLiveRange();
  /** 가로 구간 설정 (최소 폭 보장) */
  void setRange(double lo, double hi);
  /** 그래프 하나의 세로축 범위 계산·적용 */
  void updatePanelAxis(const QString & id, PanelRef & p);

  TelemetryBuffer & buffer_;
  QHash<QString, PanelRef> panels_;
  QTimer refreshTimer_;
  QVariantList stateSpans_;

  double xMin_{0.0};
  double xMax_{20.0};
  double windowS_{20.0};
  double cursorTime_;
  bool paused_{false};
  bool dirty_{true};
  int revision_{0};
};

}  // namespace amr

#endif  // AMR_TUNER_UI_PLOT_BRIDGE_HPP
