/**
 * @file    gateway_client.hpp
 * @brief   중계 노드(amr_param_gateway) TCP 클라이언트
 *
 * ---------------------------------------------------------------------------
 * [하는 일]
 *   1. 접속·끊기와 자동 재접속 (중계 노드를 껐다 켜도 앱을 다시 실행할 필요 없음)
 *   2. 요청에 번호(id)를 붙여 보내고, 같은 번호의 응답이 오면 등록한 처리 함수를 부른다
 *   3. 제한 시간 안에 응답이 없으면 CLIENT_TIMEOUT으로 처리 함수를 부른다
 *   4. 알림(param_changed 등)은 eventReceived 신호로 알린다
 *
 * [스레드]
 *   모든 처리는 화면 스레드의 이벤트 루프에서 일어난다 (QTcpSocket 비동기 신호).
 *   별도 스레드나 잠금이 필요 없고, 처리 함수에서 화면을 바로 고쳐도 안전하다.
 *
 * [연결이 끊길 때]
 *   대기 중인 요청은 모두 DISCONNECTED로 처리 함수를 부른 뒤 지운다.
 *   (응답을 영원히 기다리며 버튼이 잠긴 채로 남는 일을 막음)
 *
 * [접속 제한 시간]
 *   접속 시도가 connectTimeoutMs(기본 3000 ms) 안에 끝나지 않으면 스스로 포기한다.
 *   Windows는 닫힌 포트에 접속할 때 거절(RST)을 받아도 SYN을 몇 번 더 보내 2초 이상 걸리고,
 *   응답 없는 주소(전원 꺼진 PC, 잘못된 IP)는 약 21초 동안 "접속 중"에 머문다.
 *   OS에 맡기지 않고 앱이 시간을 정해, 화면이 오래 "접속 중"으로 멈춰 보이지 않게 한다.
 * ---------------------------------------------------------------------------
 */
#ifndef AMR_TUNER_CORE_GATEWAY_CLIENT_HPP
#define AMR_TUNER_CORE_GATEWAY_CLIENT_HPP

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>

#include <functional>

#include "core/gateway_protocol.hpp"

namespace amr
{

/** 요청 하나의 결과 */
struct Reply
{
  bool ok{false};
  QString code;          ///< 실패 시: BAD_REQUEST / NODE_NOT_FOUND / TIMEOUT / REJECTED
                         ///<          CLIENT_TIMEOUT / DISCONNECTED / NOT_CONNECTED (앱 쪽)
  QString message;       ///< 실패 사유
  QJsonObject body;      ///< 응답 전체
};

class GatewayClient : public QObject
{
  Q_OBJECT

public:
  using ReplyHandler = std::function<void (const Reply &)>;

  enum class State : quint8 { Disconnected = 0U, Connecting = 1U, Connected = 2U };

  explicit GatewayClient(QObject * parent = nullptr);
  ~GatewayClient() override;

  /** 접속 시작. autoReconnect=true면 끊겼을 때 reconnectMs마다 다시 시도한다. */
  void connectTo(const QString & host, quint16 port, bool autoReconnect = true);

  /** 사용자 요청으로 끊기 (자동 재접속도 멈춤) */
  void disconnectFrom();

  State state() const {return state_;}
  bool isConnected() const {return State::Connected == state_;}
  QString host() const {return host_;}
  quint16 port() const {return port_;}

  /** 요청 보내기. 반환값은 요청 번호 (보내지 못하면 -1, 처리 함수는 즉시 실패로 호출됨) */
  qint64 send(const QString & cmd, const QJsonObject & body, ReplyHandler handler);

  /** 응답 대기 한도 [ms] (기본 3000) */
  void setRequestTimeoutMs(int ms) {timeoutMs_ = ms;}
  /** 재접속 간격 [ms] (기본 2000) */
  void setReconnectIntervalMs(int ms);
  /** 접속 시도 제한 시간 [ms] (기본 3000). 넘으면 접속 실패로 처리 (자동 재접속이면 다시 예약) */
  void setConnectTimeoutMs(int ms);

  /** 대기 중인 요청 수 (시험·화면 표시용) */
  int pendingCount() const {return static_cast<int>(pending_.size());}

signals:
  void stateChanged(amr::GatewayClient::State state);
  /** 알림 수신 (evt 이름과 전체 객체) */
  void eventReceived(const QString & evt, const QJsonObject & body);

private slots:
  void onConnected();
  void onDisconnected();
  void onReadyRead();
  void onSocketError(QAbstractSocket::SocketError err);
  void onTimeoutTick();
  void onReconnectTimer();
  void onConnectTimeout();

private:
  struct Pending
  {
    QString cmd;
    ReplyHandler handler;
    qint64 deadlineMs{0};   ///< clock_ 기준 마감 시각
  };

  void setState(State s);
  /** 소켓 접속 시작 + 제한 시간 감시 시작 (처음 접속·재접속 공통) */
  void startConnect();
  /** 접속 실패 공통 처리: 상태 정리, 자동 재접속 예약 */
  void connectFailed();
  void handleLine(const QByteArray & line);
  /** 대기 중인 요청을 모두 실패로 끝낸다 */
  void failAll(const QString & code, const QString & message);

  QTcpSocket socket_;
  proto::LineBuffer lines_;
  QHash<qint64, Pending> pending_;
  QElapsedTimer clock_;
  QTimer timeoutTimer_;       ///< 응답 마감 확인 (100 ms 주기)
  QTimer reconnectTimer_;     ///< 자동 재접속
  QTimer connectTimer_;       ///< 접속 시도 제한 시간

  QString host_;
  quint16 port_{0U};
  bool autoReconnect_{false};
  bool userDisconnect_{false};
  bool errorLogged_{false};   ///< 재접속 중 같은 오류 로그를 반복하지 않기 위함
  State state_{State::Disconnected};
  qint64 nextId_{1};
  int timeoutMs_{3000};
};

}  // namespace amr

#endif  // AMR_TUNER_CORE_GATEWAY_CLIENT_HPP
