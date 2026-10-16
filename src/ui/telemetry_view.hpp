/**
 * @file    telemetry_view.hpp
 * @brief   그래프 탭 (위젯 도구줄 + QML 그래프 화면)
 *
 * ┌──────────────────────────────────────────────────────────────────────────┐
 * │ [일시정지] 보기 [20 s ▾] ☑ 목표를 받으면 새로 그리기 [지우기] [화면 저장] ☐ 메모 입력  이동 #3 · 12.4 s · 20.0 Hz │
 * ├──────────────────────────────────────────────────────────────────────────┤
 * │ 상태 ■정지 ■방향 회전 ■직진 ■최종 회전 ■도착                                 │
 * │ ┌ 선속도 [m/s] ───────────┐ ┌ 거리 PID 항 [m/s] ─────────┐                   │
 * │ │                          │ │                             │                  │
 * │ ┌ 궤적 추종 오차 [mm] ────┐ ┌ 방향 PID 항 [rad/s] ───────┐  (QML, Qt Graphs) │
 * │ ┌ 목표 거리 오차 [m] ─────┐ ┌ 방향 오차 [°] ─────────────┐                   │
 * └──────────────────────────────────────────────────────────────────────────┘
 *
 * [데이터 흐름]
 *   MainWindow::onEvent ─telemetry─▶ onTelemetry ─▶ TelemetryBuffer ─▶ PlotBridge(50 ms) ─▶ QML 선
 *                       ─goal──────▶ onGoal      ─▶ 새 이동 시작 (자동 시작이 켜져 있으면)
 *
 * [화면 저장] (0.3.0)
 *   버튼을 누르면 captureRequested 신호만 낸다. 저장(파일 이름, 메모 창, 적용된 파라미터 기록)은
 *   파라미터 모델을 가진 MainWindow가 grabImage()·captureInfo()로 받아 처리한다 (core/capture_record.hpp).
 *
 * [일시정지 중에 들어온 데이터]
 *   보고 있는 그래프가 새 이동으로 지워지지 않도록, 일시정지 동안 받은 알림은 대기열에 두었다가
 *   다시 실시간으로 돌아갈 때 받은 순서대로 반영한다 (대기열 한도를 넘으면 오래된 것부터 버림).
 */
#ifndef AMR_TUNER_UI_TELEMETRY_VIEW_HPP
#define AMR_TUNER_UI_TELEMETRY_VIEW_HPP

#include <QJsonObject>
#include <QTimer>
#include <QWidget>

#include <deque>

#include "core/capture_record.hpp"
#include "core/gateway_protocol.hpp"
#include "core/telemetry_buffer.hpp"
#include "ui/plot_bridge.hpp"

class QImage;

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QQuickWidget;

namespace amr
{

class TelemetryView : public QWidget
{
  Q_OBJECT

public:
  explicit TelemetryView(QWidget * parent = nullptr);
  ~TelemetryView() override;

  /** telemetry 알림 본문 하나 */
  void onTelemetry(const QJsonObject & body);
  /** goal 알림 본문 하나 */
  void onGoal(const QJsonObject & body);

  /** 접속 상태 (끊기면 표시만 바꾸고 그래프는 남김) */
  void setConnected(bool connected);
  /** 중계 노드가 그래프 알림을 지원하지 않을 때 이유 표시 (빈 문자열이면 지움) */
  void setUnavailable(const QString & reason);

  void loadSettings();
  void saveSettings() const;

  TelemetryBuffer & buffer() {return buffer_;}

  /**
   * 그래프 탭 전체 이미지 (도구줄·상태 글 + QML 그래프).
   * QML 그래프는 QQuickWidget이 따로 그리므로 그 화면(grabFramebuffer)을 같은 자리에 덮어 그린다.
   * 고해상도 화면에서는 실제 픽셀 크기. 실패하면 빈 이미지.
   */
  QImage grabImage();

  /** 저장 순간의 그래프 정보 (이동 번호, 시각, 보이는 구간, 일시정지·커서, 마지막 목표). image는 비워 둠 */
  CaptureInfo captureInfo() const;

  /** [메모 입력]이 켜져 있는지 */
  bool memoEnabled() const;

signals:
  /** [화면 저장]을 눌렀다 */
  void captureRequested();

private slots:
  void onPauseToggled(bool on);
  void onWindowChanged(int index);
  void clearPlots();
  void updateStatus();

private:
  struct Queued
  {
    bool isGoal{false};
    QJsonObject body;
  };

  void buildUi();
  void loadQml();
  /** 대기열 없이 바로 반영 */
  void applyTelemetry(const QJsonObject & body);
  void applyGoal(const QJsonObject & body);
  /** 일시정지 동안 쌓인 알림 반영 */
  void drainQueue();

  TelemetryBuffer buffer_;
  PlotBridge bridge_;
  std::deque<Queued> queue_;
  QTimer statusTimer_;
  bool connected_{false};
  bool badLineLogged_{false};
  QString unavailable_;
  qint64 droppedQueued_{0};
  bool hasGoal_{false};
  proto::GoalInfo lastGoal_;   ///< 마지막으로 받은 목표 (화면 저장 기록용)

  /* 위젯 (부모가 소유) */
  QPushButton * pauseBtn_{nullptr};
  QComboBox * windowCombo_{nullptr};
  QCheckBox * autoStartChk_{nullptr};
  QPushButton * clearBtn_{nullptr};
  QPushButton * captureBtn_{nullptr};
  QCheckBox * memoChk_{nullptr};
  QLabel * statusLabel_{nullptr};
  QQuickWidget * quick_{nullptr};
  QLabel * errorLabel_{nullptr};
};

}  // namespace amr

#endif  // AMR_TUNER_UI_TELEMETRY_VIEW_HPP
