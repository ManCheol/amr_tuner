/**
 * @file    test_gateway_client.cpp
 * @brief   통신부 시험 — 가짜 중계 서버와 실제 TCP로 주고받는다
 */
#include <QElapsedTimer>
#include <QJsonArray>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

#include "core/gateway_client.hpp"
#include "fake_gateway_server.hpp"

using amr::GatewayClient;
using amr::Reply;
using amr::test::FakeGatewayServer;

namespace
{
constexpr int kWaitMs = 3000;

/** 요청을 보내고 응답이 올 때까지 기다린다 */
Reply call(GatewayClient & c, const QString & cmd, const QJsonObject & body = QJsonObject())
{
  Reply out;
  bool done = false;
  (void)c.send(cmd, body, [&](const Reply & r) {out = r; done = true;});
  if (false == QTest::qWaitFor([&]() {return done;}, kWaitMs)) {
    out.code = QStringLiteral("TEST_WAIT_TIMEOUT");
  }
  return out;
}
}  // namespace

class TestGatewayClient : public QObject
{
  Q_OBJECT

private:
  std::unique_ptr<FakeGatewayServer> server_;
  std::unique_ptr<GatewayClient> client_;

  bool connectClient(bool autoReconnect = false)
  {
    client_->connectTo(QStringLiteral("127.0.0.1"), server_->port(), autoReconnect);
    return QTest::qWaitFor([this]() {return client_->isConnected();}, kWaitMs);
  }

private slots:
  void init()
  {
    server_ = std::make_unique<FakeGatewayServer>();
    QVERIFY(server_->start());
    client_ = std::make_unique<GatewayClient>();
    client_->setRequestTimeoutMs(1000);
  }

  void cleanup()
  {
    client_.reset();
    server_.reset();
  }

  void connectAndHello()
  {
    QSignalSpy spy(client_.get(), &GatewayClient::stateChanged);
    QVERIFY(connectClient());
    QVERIFY(spy.count() >= 2);   // Connecting → Connected
    const Reply r = call(*client_, QStringLiteral("hello"));
    QVERIFY2(r.ok, qPrintable(r.code));
    QCOMPARE(r.body.value(QStringLiteral("node")).toString(), QStringLiteral("/pid_goal_controller"));
    QVERIFY(r.body.value(QStringLiteral("node_ready")).toBool());
  }

  void sendWhenNotConnectedFailsImmediately()
  {
    bool called = false;
    QString code;
    const qint64 id = client_->send(QStringLiteral("ping"), QJsonObject(),
      [&](const Reply & r) {called = true; code = r.code;});
    QCOMPARE(id, -1);
    QVERIFY(called);   // 기다리지 않고 바로 실패
    QCOMPARE(code, QStringLiteral("NOT_CONNECTED"));
  }

  void responsesMatchedById()
  {
    QVERIFY(connectClient());
    /* 응답 순서와 관계없이 각 처리 함수가 자기 응답을 받아야 한다 */
    int done = 0;
    bool pingOk = false;
    bool listOk = false;
    (void)client_->send(QStringLiteral("list"), QJsonObject(), [&](const Reply & r) {
        listOk = r.ok && r.body.contains(QStringLiteral("params")); ++done;
      });
    (void)client_->send(QStringLiteral("ping"), QJsonObject(), [&](const Reply & r) {
        pingOk = r.ok && r.body.value(QStringLiteral("pong")).toBool(); ++done;
      });
    QVERIFY(QTest::qWaitFor([&]() {return 2 == done;}, kWaitMs));
    QVERIFY(pingOk);
    QVERIFY(listOk);
    QCOMPARE(client_->pendingCount(), 0);
  }

  void setAtomicAcceptAndReject()
  {
    QVERIFY(connectClient());
    Reply r = call(*client_, QStringLiteral("set"), QJsonObject{{QStringLiteral("params"),
      QJsonObject{{QStringLiteral("linear.kp"), 1.0}, {QStringLiteral("linear.kd"), 0.2}}}});
    QVERIFY2(r.ok, qPrintable(r.message));
    QCOMPARE(r.body.value(QStringLiteral("applied")).toObject().size(), 2);

    /* 하나가 범위 밖이면 전부 거부 → kp는 1.0 그대로 */
    r = call(*client_, QStringLiteral("set"), QJsonObject{{QStringLiteral("params"),
      QJsonObject{{QStringLiteral("linear.kp"), 2.0}, {QStringLiteral("linear.output_limit"), 0.5}}}});
    QVERIFY(false == r.ok);
    QCOMPARE(r.code, QStringLiteral("REJECTED"));
    QVERIFY(r.message.contains(QStringLiteral("linear.output_limit")));
    for (const auto & p : server_->params()) {
      if (QLatin1String("linear.kp") == p.name) {
        QCOMPARE(p.value.toDouble(), 1.0);
      }
    }
  }

  void eventsAfterSubscribe()
  {
    QVERIFY(connectClient());
    QVERIFY(call(*client_, QStringLiteral("subscribe"),
      QJsonObject{{QStringLiteral("events"), QJsonArray{QStringLiteral("param_changed")}}}).ok);
    QSignalSpy spy(client_.get(), &GatewayClient::eventReceived);
    server_->pushParamChanged(QJsonObject{{QStringLiteral("linear.kp"), 0.9}});   // rqt에서 바꾼 상황
    QVERIFY(spy.wait(kWaitMs));
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("param_changed"));
    const QJsonObject body = spy.at(0).at(1).toJsonObject();
    QCOMPARE(body.value(QStringLiteral("params")).toObject().value(QStringLiteral("linear.kp")).toDouble(), 0.9);
  }

  void timeoutWhenServerSilent()
  {
    client_->setRequestTimeoutMs(300);
    QVERIFY(connectClient());
    server_->setSilentCommand(QStringLiteral("ping"));
    QElapsedTimer t;
    t.start();
    const Reply r = call(*client_, QStringLiteral("ping"));
    QCOMPARE(r.code, QStringLiteral("CLIENT_TIMEOUT"));
    QVERIFY(t.elapsed() >= 250);
    QVERIFY(t.elapsed() < 2000);
    QCOMPARE(client_->pendingCount(), 0);
    /* 시간 초과 뒤에도 연결은 계속 쓸 수 있다 */
    server_->setSilentCommand(QString());
    QVERIFY(call(*client_, QStringLiteral("ping")).ok);
  }

  void splitWritesAreReassembled()
  {
    QVERIFY(connectClient());
    server_->setSplitWrites(true);
    const Reply r = call(*client_, QStringLiteral("list"));
    QVERIFY2(r.ok, qPrintable(r.code));
    QCOMPARE(r.body.value(QStringLiteral("params")).toArray().size(), server_->params().size());
  }

  void serverDropFailsPendingAndReconnects()
  {
    client_->setReconnectIntervalMs(100);
    QVERIFY(connectClient(true));
    server_->setSilentCommand(QStringLiteral("ping"));
    QString code;
    bool done = false;
    (void)client_->send(QStringLiteral("ping"), QJsonObject(), [&](const Reply & r) {code = r.code; done = true;});
    QVERIFY(QTest::qWaitFor([this]() {return 1 == server_->clientCount();}, kWaitMs));
    server_->dropAll();   // 중계 노드 재시작 상황
    QVERIFY(QTest::qWaitFor([&]() {return done;}, kWaitMs));
    QCOMPARE(code, QStringLiteral("DISCONNECTED"));
    /* 자동 재접속 */
    QVERIFY(QTest::qWaitFor([this]() {return client_->isConnected();}, kWaitMs));
    server_->setSilentCommand(QString());
    QVERIFY(call(*client_, QStringLiteral("ping")).ok);
  }

  void userDisconnectStopsReconnect()
  {
    client_->setReconnectIntervalMs(100);
    QVERIFY(connectClient(true));
    client_->disconnectFrom();
    QCOMPARE(client_->state(), GatewayClient::State::Disconnected);
    QTest::qWait(400);
    QCOMPARE(client_->state(), GatewayClient::State::Disconnected);
  }

  /* 닫힌 포트: 거절(RST) 또는 접속 제한 시간 중 먼저 오는 쪽으로 실패 처리.
   * Windows는 RST를 받아도 SYN을 다시 보내 2초 넘게 걸릴 수 있어(실측 3초 이상),
   * 앱의 접속 제한 시간(여기서는 1초)이 없으면 kWaitMs 안에 끝나지 않았다. */
  void connectionRefused()
  {
    const quint16 port = server_->port();
    server_.reset();   // 대기 중인 서버 없음
    client_->setConnectTimeoutMs(1000);
    QElapsedTimer t;
    t.start();
    client_->connectTo(QStringLiteral("127.0.0.1"), port, false);
    QVERIFY(QTest::qWaitFor([this]() {
        return GatewayClient::State::Disconnected == client_->state();
      }, kWaitMs));
    QVERIFY2(t.elapsed() < 2000, qPrintable(QStringLiteral("took %1 ms").arg(t.elapsed())));
    QTest::qWait(300);   // 자동 재접속을 끈 상태: 다시 시도하지 않음
    QCOMPARE(client_->state(), GatewayClient::State::Disconnected);
  }

  /* 응답 없는 주소(RFC 5737 문서용 주소, 라우팅되지 않음): OS는 약 21초(Windows) 동안
   * 접속 중에 머물 수 있다. 앱의 제한 시간으로 끊고, 자동 재접속이면 다시 시도해야 한다. */
  void unreachableHostGivesUpAndRetries()
  {
    client_->setConnectTimeoutMs(300);
    client_->setReconnectIntervalMs(200);
    QElapsedTimer t;
    t.start();
    client_->connectTo(QStringLiteral("198.51.100.1"), 9310, true);
    QVERIFY(QTest::qWaitFor([this]() {
        return GatewayClient::State::Disconnected == client_->state();
      }, kWaitMs));
    QVERIFY2(t.elapsed() < 1500, qPrintable(QStringLiteral("took %1 ms").arg(t.elapsed())));
    /* 재접속 예약 → 다시 Connecting으로 */
    QVERIFY(QTest::qWaitFor([this]() {
        return GatewayClient::State::Connecting == client_->state();
      }, kWaitMs));
    client_->disconnectFrom();
    QCOMPARE(client_->state(), GatewayClient::State::Disconnected);
  }

  void serverFullEvent()
  {
    server_->setMaxClients(0);
    QSignalSpy spy(client_.get(), &GatewayClient::eventReceived);
    client_->connectTo(QStringLiteral("127.0.0.1"), server_->port(), false);
    QVERIFY(QTest::qWaitFor([&]() {return 0 < spy.count();}, kWaitMs));
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("server_full"));
  }
};

QTEST_GUILESS_MAIN(TestGatewayClient)
#include "test_gateway_client.moc"
