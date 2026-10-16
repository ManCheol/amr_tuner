/**
 * @file    test_gateway_protocol.cpp
 * @brief   규약 단위 시험 — 요청 생성, 응답·알림 해석, 값 변환, 줄 나누기, 그래프 알림
 */
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

#include "core/gateway_protocol.hpp"

#include <cmath>

using namespace amr::proto;

class TestGatewayProtocol : public QObject
{
  Q_OBJECT

private slots:
  /* ---------------- 요청 ---------------- */
  void makeRequestHasEnvelopeAndNewline()
  {
    const QByteArray line = makeRequest(7, QStringLiteral("get"),
      QJsonObject{{QStringLiteral("names"), QJsonArray{QStringLiteral("linear.kp")}}});
    QVERIFY(line.endsWith('\n'));
    QCOMPARE(line.count('\n'), 1);   // 한 줄
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    QCOMPARE(o.value(QStringLiteral("v")).toInt(), 1);
    QCOMPARE(o.value(QStringLiteral("id")).toInteger(), 7);
    QCOMPARE(o.value(QStringLiteral("type")).toString(), QStringLiteral("req"));
    QCOMPARE(o.value(QStringLiteral("cmd")).toString(), QStringLiteral("get"));
    QCOMPARE(o.value(QStringLiteral("names")).toArray().at(0).toString(), QStringLiteral("linear.kp"));
  }

  void makeRequestKeepsKoreanUtf8()
  {
    const QByteArray line = makeRequest(1, QStringLiteral("x"),
      QJsonObject{{QStringLiteral("note"), QStringLiteral("직선 이득")}});
    QCOMPARE(QJsonDocument::fromJson(line).object().value(QStringLiteral("note")).toString(),
      QStringLiteral("직선 이득"));
  }

  /* ---------------- 응답·알림 ---------------- */
  void parseOkResponse()
  {
    const Message m = parseMessage(R"({"v":1,"id":3,"type":"res","ok":true,"pong":true})");
    QCOMPARE(m.kind, Message::Kind::Response);
    QCOMPARE(m.id, 3);
    QVERIFY(m.ok);
    QVERIFY(m.body.value(QStringLiteral("pong")).toBool());
  }

  void parseErrorResponse()
  {
    const Message m = parseMessage(
      R"({"v":1,"id":4,"type":"res","ok":false,"error":{"code":"REJECTED","message":"out of range"}})");
    QCOMPARE(m.kind, Message::Kind::Response);
    QVERIFY(false == m.ok);
    QCOMPARE(m.errorCode, QStringLiteral("REJECTED"));
    QCOMPARE(m.errorMessage, QStringLiteral("out of range"));
  }

  void parseEvent()
  {
    const Message m = parseMessage(
      R"({"v":1,"type":"evt","evt":"param_changed","node":"/n","params":{"linear.kp":1.0}})");
    QCOMPARE(m.kind, Message::Kind::Event);
    QCOMPARE(m.event, QStringLiteral("param_changed"));
    QCOMPARE(m.body.value(QStringLiteral("params")).toObject().value(QStringLiteral("linear.kp")).toDouble(), 1.0);
  }

  void parseServerFullWithoutVersionIsOk()
  {
    const Message m = parseMessage(R"({"type":"evt","evt":"server_full"})");
    QCOMPARE(m.kind, Message::Kind::Event);
  }

  void parseInvalid_data()
  {
    QTest::addColumn<QByteArray>("line");
    QTest::newRow("broken json") << QByteArray("{oops");
    QTest::newRow("array") << QByteArray("[1,2]");
    QTest::newRow("wrong version") << QByteArray(R"({"v":2,"id":1,"type":"res","ok":true})");
    QTest::newRow("res without id") << QByteArray(R"({"v":1,"type":"res","ok":true})");
    QTest::newRow("unknown type") << QByteArray(R"({"v":1,"id":1,"type":"req","cmd":"x"})");
    QTest::newRow("event without name") << QByteArray(R"({"v":1,"type":"evt"})");
  }
  void parseInvalid()
  {
    QFETCH(QByteArray, line);
    const Message m = parseMessage(line);
    QCOMPARE(m.kind, Message::Kind::Invalid);
    QVERIFY(false == m.parseError.isEmpty());
  }

  /* ---------------- list 응답 ---------------- */
  void parseParamListWithRangeAndReadOnly()
  {
    const QJsonObject body = QJsonDocument::fromJson(R"({"node":"/n","params":[
      {"name":"linear.kp","type":"double","value":0.5,"read_only":false,"description":"P 이득",
       "range":{"min":0,"max":10,"step":0}},
      {"name":"odom_topic","type":"string","value":"odom","read_only":true,"description":""},
      {"name":"arr","type":"unsupported","read_only":false,"description":""}]})").object();
    QList<ParamInfo> list;
    QString err;
    QVERIFY2(parseParamList(body, list, err), qPrintable(err));
    QCOMPARE(list.size(), 3);
    QCOMPARE(list[0].type, ParamType::Double);
    QCOMPARE(list[0].value.toDouble(), 0.5);
    QVERIFY(list[0].hasRange);
    QCOMPARE(list[0].max, 10.0);
    QCOMPARE(list[0].description, QStringLiteral("P 이득"));
    QVERIFY(list[1].readOnly);
    QCOMPARE(list[1].value.toString(), QStringLiteral("odom"));
    QCOMPARE(list[2].type, ParamType::Unsupported);
    QVERIFY(false == list[2].value.isValid());
  }

  void parseParamListRejectsBadBody()
  {
    QList<ParamInfo> list;
    QString err;
    QVERIFY(false == parseParamList(QJsonObject(), list, err));
    QVERIFY(false == parseParamList(QJsonDocument::fromJson(R"({"params":[{"type":"bool"}]})").object(),
      list, err));
    QVERIFY(list.isEmpty());
  }

  /* ---------------- 값 변환 ---------------- */
  void fromJsonRules()
  {
    QCOMPARE(fromJson(QJsonValue(1), ParamType::Double).toDouble(), 1.0);   // 정수 → 실수 허용
    QVERIFY(false == fromJson(QJsonValue(1.5), ParamType::Int).isValid());  // 소수 → 정수 거부
    QCOMPARE(fromJson(QJsonValue(3), ParamType::Int).toLongLong(), 3);
    QVERIFY(false == fromJson(QJsonValue(1), ParamType::Bool).isValid());   // 1 ≠ true
    QCOMPARE(fromJson(QJsonValue(true), ParamType::Bool).toBool(), true);
    QVERIFY(false == fromJson(QJsonValue(3), ParamType::String).isValid());
    QVERIFY(false == fromJson(QJsonValue(QStringLiteral("0.5")), ParamType::Double).isValid());
  }

  void toJsonKeepsKind()
  {
    QVERIFY(toJson(QVariant(true), ParamType::Bool).isBool());
    QVERIFY(toJson(QVariant(0.5), ParamType::Double).isDouble());
    QCOMPARE(toJson(QVariant(static_cast<qint64>(5)), ParamType::Int).toInteger(), 5);
    QVERIFY(toJson(QVariant(QStringLiteral("x")), ParamType::String).isString());
  }

  void displayValueFormats()
  {
    QCOMPARE(displayValue(20.0, ParamType::Double), QStringLiteral("20.0"));
    QCOMPARE(displayValue(0.05, ParamType::Double), QStringLiteral("0.05"));
    QCOMPARE(displayValue(0.5236, ParamType::Double), QStringLiteral("0.5236"));
    QCOMPARE(displayValue(true, ParamType::Bool), QStringLiteral("true"));
    QCOMPARE(displayValue(static_cast<qint64>(1000), ParamType::Int), QStringLiteral("1000"));
    QCOMPARE(displayValue(QVariant(), ParamType::Double), QString());
  }

  void sameValueTolerance()
  {
    QVERIFY(sameValue(0.1 + 0.2, 0.3, ParamType::Double));   // 부동소수 오차는 같은 값
    QVERIFY(false == sameValue(0.3, 0.3001, ParamType::Double));
    QVERIFY(sameValue(true, true, ParamType::Bool));
    QVERIFY(false == sameValue(QStringLiteral("a"), QStringLiteral("b"), ParamType::String));
  }

  /* ---------------- 줄 나누기 ---------------- */
  void lineBufferSplitAndJoin()
  {
    LineBuffer b(64);
    QVERIFY(b.append("ab").isEmpty());
    QList<QByteArray> l = b.append("c\nde\r\n\nf");
    QCOMPARE(l.size(), 2);
    QCOMPARE(l[0], QByteArray("abc"));
    QCOMPARE(l[1], QByteArray("de"));   // \r 제거, 빈 줄 생략
    l = b.append("\n");
    QCOMPARE(l.size(), 1);
    QCOMPARE(l[0], QByteArray("f"));
  }

  void lineBufferOverflow()
  {
    LineBuffer b(8);
    (void)b.append("12345678");
    QVERIFY(false == b.overflow());
    (void)b.append("9");
    QVERIFY(b.overflow());
    QVERIFY(b.append("\n").isEmpty());
    b.clear();
    QVERIFY(false == b.overflow());
  }

  /* ---------------- 그래프 알림 (중계 0.2.0) ---------------- */
  void parseTelemetryFull()
  {
    /* 중계 노드 시험(test_telemetry)과 같은 모양의 실제 한 줄 */
    const Message m = parseMessage(
      R"({"v":1,"type":"evt","evt":"telemetry","t":12.346,"state":2,"cmd":[0.12,-0.3],)"
      R"("lin":[0.01,0.05,0,0,0.05,0],"ang":[0.1,0.15,0,0,0.15,1],)"
      R"("traj":[0.5,0.2,0,0.498,0.002,2.5,6,1],"err":[1.5,0.02,-1]})");
    QCOMPARE(m.kind, Message::Kind::Event);
    QCOMPARE(m.event, QStringLiteral("telemetry"));
    TelemetrySample s;
    QString err;
    QVERIFY2(parseTelemetry(m.body, s, err), qPrintable(err));
    QCOMPARE(s.t, 12.346);
    QCOMPARE(s.state, 2);
    QVERIFY(s.hasCmd && s.hasLin && s.hasAng && s.hasTraj && s.hasErr);
    QCOMPARE(s.cmdV, 0.12);
    QCOMPARE(s.cmdW, -0.3);
    QCOMPARE(s.lin[4], 0.05);
    QCOMPARE(s.ang[5], 1.0);
    QCOMPARE(s.traj[4], 0.002);
    QCOMPARE(s.err[2], -1.0);
    QVERIFY(false == s.hasPath);   // 0.2.0 중계 노드: path 없음

    /* 0.3.0: 경로 추종 값 */
    const Message m3 = parseMessage(
      R"({"v":1,"type":"evt","evt":"telemetry","t":1.0,"state":2,)"
      R"("path":[0.0123,-0.05,3.8,0.2,0.75,1.88,1]})");
    TelemetrySample s3;
    QVERIFY2(parseTelemetry(m3.body, s3, err), qPrintable(err));
    QVERIFY(s3.hasPath);
    QCOMPARE(s3.path[0], 0.0123);
    QCOMPARE(s3.path[3], 0.2);
    QCOMPARE(s3.path[6], 1.0);
    /* 짧은 배열은 그 줄 전체를 버림 (다른 배열과 같은 규칙) */
    const Message bad = parseMessage(
      R"({"v":1,"type":"evt","evt":"telemetry","t":1.0,"state":2,"path":[0.01,0,1,0.2,0.5,1]})");
    QVERIFY(false == parseTelemetry(bad.body, s3, err));
  }

  void parseTelemetryOptionalAndNull()
  {
    /* 목표 전: err 없음, 아직 받지 못한 묶음 없음, NaN은 null */
    TelemetrySample s;
    QString err;
    QVERIFY(parseTelemetry(QJsonDocument::fromJson(
      R"({"t":1,"state":0,"cmd":[null,0],"err":[null,0.5,null]})").object(), s, err));
    QVERIFY(s.hasCmd);
    QVERIFY(std::isnan(s.cmdV));
    QVERIFY(false == s.hasLin);
    QVERIFY(false == s.hasTraj);
    QVERIFY(s.hasErr);
    QVERIFY(std::isnan(s.err[0]));
    QCOMPARE(s.err[1], 0.5);
  }

  void parseTelemetryRejectsBad_data()
  {
    QTest::addColumn<QByteArray>("json");
    QTest::newRow("no t") << QByteArray(R"({"state":1})");
    QTest::newRow("no state") << QByteArray(R"({"t":1})");
    QTest::newRow("short lin") << QByteArray(R"({"t":1,"state":1,"lin":[1,2,3]})");
    QTest::newRow("lin not array") << QByteArray(R"({"t":1,"state":1,"lin":5})");
    QTest::newRow("string value") << QByteArray(R"({"t":1,"state":1,"cmd":["a",0]})");
  }

  void parseTelemetryRejectsBad()
  {
    QFETCH(QByteArray, json);
    TelemetrySample s;
    QString err;
    QVERIFY(false == parseTelemetry(QJsonDocument::fromJson(json).object(), s, err));
    QVERIFY(false == err.isEmpty());
  }

  void parseGoalEvent()
  {
    GoalInfo g;
    QString err;
    QVERIFY(parseGoal(QJsonDocument::fromJson(
      R"({"t":5,"x":1,"y":-0.5,"yaw":3.141593,"frame":"map","stamp":"2026-10-14T21:00:00+09:00"})")
      .object(), g, err));
    QCOMPARE(g.t, 5.0);
    QCOMPARE(g.y, -0.5);
    QCOMPARE(g.frame, QStringLiteral("map"));
    QVERIFY(false == parseGoal(QJsonDocument::fromJson(R"({"x":1})").object(), g, err));
  }

  void supportedEventsFromHello()
  {
    /* 0.2.0: 목록 그대로, 0.1.0: events 없음 → param_changed만 */
    QCOMPARE(supportedEvents(QJsonDocument::fromJson(
      R"({"version":"0.2.0","events":["param_changed","telemetry","goal"]})").object()),
      QStringList({QStringLiteral("param_changed"), QStringLiteral("telemetry"), QStringLiteral("goal")}));
    QCOMPARE(supportedEvents(QJsonDocument::fromJson(R"({"version":"0.1.0"})").object()),
      QStringList({QStringLiteral("param_changed")}));
  }
};

QTEST_GUILESS_MAIN(TestGatewayProtocol)
#include "test_gateway_protocol.moc"
