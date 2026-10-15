/**
 * @file    main_window.hpp
 * @brief   AMR Tuner 주 화면
 *
 * ┌───────────────────────────────────────────────────────────────────────┐
 * │ 주소 [127.0.0.1] 포트 [9310] [접속]   ● 연결됨 · /pid_goal_controller  │
 * │ 검색 [        ] □ ROS 내부 숨김 □ 변경 예정만 [새로 고침]               │
 * │                     변경 예정 2개 [적용] [되돌리기] [조건 저장] [불러오기] │
 * ├─[파라미터]─[그래프]────────────────────────────────────────────────────┤
 * │ 이름 | 현재 값 | 변경 값 | 범위 | 설명        (그래프 탭: TelemetryView) │
 * │  ...                                                                  │
 * ├───────────────────────────────────────────────────────────────────────┤
 * │ 로그                                                                  │
 * └───────────────────────────────────────────────────────────────────────┘
 *
 * [접속 순서]  접속 → hello(노드 확인, 지원 알림) → list(목록)
 *              → subscribe(param_changed + 지원하면 telemetry·goal)
 * [적용]       변경 예정 값을 set 하나로 보냄 (전부 적용 또는 전부 거부)
 */
#ifndef AMR_TUNER_UI_MAIN_WINDOW_HPP
#define AMR_TUNER_UI_MAIN_WINDOW_HPP

#include <QMainWindow>
#include <QTimer>

#include "core/app_log.hpp"
#include "core/gateway_client.hpp"
#include "core/param_model.hpp"
#include "ui/param_filter_proxy.hpp"
#include "ui/telemetry_view.hpp"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTabWidget;
class QTableView;

namespace amr
{

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget * parent = nullptr);
  ~MainWindow() override;

  /* 시험·화면 캡처용 접근자 */
  GatewayClient & client() {return client_;}
  ParamModel & model() {return model_;}

protected:
  void closeEvent(QCloseEvent * event) override;
  bool eventFilter(QObject * watched, QEvent * event) override;

private slots:
  void onConnectClicked();
  void onStateChanged(amr::GatewayClient::State state);
  void onEvent(const QString & evt, const QJsonObject & body);
  void refresh();
  void apply();
  void revert();
  void saveCondition();
  void loadCondition();
  void onPendingCountChanged(int count);
  void onLog(const QString & line, amr::AppLog::Level level);

private:
  void buildUi();
  void loadSettings();
  void saveSettings() const;
  void requestHello();
  /** 중계 노드가 지원하는 알림 중 앱이 쓰는 것만 구독 */
  void requestSubscribe(const QStringList & supported);
  void updateButtons();
  void updateStatus();
  void updateCountLabel();
  /** 서버 오류를 한 줄 설명으로 */
  static QString describe(const Reply & r);

  GatewayClient client_;
  ParamModel model_;
  ParamFilterProxy proxy_;
  QTimer highlightTimer_;   ///< 바뀐 값의 파란색 표시를 잠시 뒤 지움

  QString nodeName_;
  QString gatewayVersion_;
  bool nodeReady_{false};
  bool busy_{false};        ///< 목록 요청이나 적용 요청을 기다리는 중

  /* 위젯 (부모 위젯이 소유) */
  QLineEdit * hostEdit_{nullptr};
  QSpinBox * portSpin_{nullptr};
  QPushButton * connectBtn_{nullptr};
  QLabel * connLabel_{nullptr};
  QLineEdit * searchEdit_{nullptr};
  QCheckBox * hideInternalChk_{nullptr};
  QCheckBox * pendingOnlyChk_{nullptr};
  QPushButton * refreshBtn_{nullptr};
  QLabel * pendingLabel_{nullptr};
  QPushButton * applyBtn_{nullptr};
  QPushButton * revertBtn_{nullptr};
  QPushButton * saveBtn_{nullptr};
  QPushButton * loadBtn_{nullptr};
  QTabWidget * tabs_{nullptr};
  QTableView * table_{nullptr};
  TelemetryView * telemetryView_{nullptr};
  QPlainTextEdit * logView_{nullptr};
  QSplitter * splitter_{nullptr};
  QLabel * nodeLabel_{nullptr};
  QLabel * countLabel_{nullptr};
};

}  // namespace amr

#endif  // AMR_TUNER_UI_MAIN_WINDOW_HPP
