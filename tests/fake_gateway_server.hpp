/**
 * @file    fake_gateway_server.hpp
 * @brief   시험용 가짜 중계 서버 (amr_param_gateway 규약 v1을 흉내 냄)
 *
 * ROS 없이 실제 TCP 소켓으로 앱의 통신부를 시험하기 위해 사용한다.
 *   - hello / ping / list / get / set(전부 적용 또는 전부 거부) / subscribe
 *   - 범위 밖·읽기 전용·없는 이름은 실제 중계 노드처럼 REJECTED
 *   - 시험 조작: 특정 명령에 응답 안 하기, 응답을 둘로 쪼개 보내기, 알림 보내기,
 *     모든 연결 끊기, 노드 없음 흉내
 */
#ifndef AMR_TUNER_TESTS_FAKE_GATEWAY_SERVER_HPP
#define AMR_TUNER_TESTS_FAKE_GATEWAY_SERVER_HPP

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include "core/gateway_protocol.hpp"

namespace amr::test
{

class FakeGatewayServer : public QObject
{
  Q_OBJECT

public:
  explicit FakeGatewayServer(QObject * parent = nullptr);
  ~FakeGatewayServer() override;

  /** 127.0.0.1에서 대기 시작 (port 0 = 빈 포트 자동 선택) */
  bool start(quint16 port = 0U);
  quint16 port() const {return server_.serverPort();}

  /** 파라미터 표 바꾸기 (기본값: PID 노드 일부를 흉내 낸 표) */
  void setParams(const QList<proto::ParamInfo> & params) {params_ = params;}
  const QList<proto::ParamInfo> & params() const {return params_;}
  /** PID 노드의 실제 파라미터 목록과 같은 표 (화면 확인용) */
  static QList<proto::ParamInfo> pidNodeParams();

  /* ---- 시험 조작 ---- */
  void setNodeReady(bool ready) {nodeReady_ = ready;}
  void setSilentCommand(const QString & cmd) {silentCmd_ = cmd;}
  void setSplitWrites(bool split) {splitWrites_ = split;}
  void setMaxClients(int n) {maxClients_ = n;}
  /** 구독 중인 클라이언트에 param_changed 알림 (표의 값도 바꿈) */
  void pushParamChanged(const QJsonObject & params);
  /** 모든 연결 끊기 */
  void dropAll();

  int clientCount() const {return static_cast<int>(clients_.size());}
  int requestCount() const {return requestCount_;}
  QString lastCommand() const {return lastCmd_;}

private slots:
  void onNewConnection();

private:
  void handleLine(QTcpSocket * s, const QByteArray & line);
  void writeLine(QTcpSocket * s, const QJsonObject & obj);
  QJsonObject ok(qint64 id, const QJsonObject & body) const;
  QJsonObject err(qint64 id, const QString & code, const QString & msg) const;
  proto::ParamInfo * find(const QString & name);

  QTcpServer server_;
  QList<QTcpSocket *> clients_;
  QHash<QTcpSocket *, proto::LineBuffer *> buffers_;
  QSet<QTcpSocket *> subscribers_;
  QList<proto::ParamInfo> params_;

  bool nodeReady_{true};
  bool splitWrites_{false};
  int maxClients_{4};
  QString silentCmd_;
  QString lastCmd_;
  int requestCount_{0};
};

}  // namespace amr::test

#endif  // AMR_TUNER_TESTS_FAKE_GATEWAY_SERVER_HPP
