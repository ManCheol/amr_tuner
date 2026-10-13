/**
 * @file    fake_gateway_server.cpp
 * @brief   시험용 가짜 중계 서버 구현
 */
#include "fake_gateway_server.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "core/kst_time.hpp"

namespace amr::test
{

using proto::ParamInfo;
using proto::ParamType;

namespace
{
ParamInfo makeParam(
  const QString & name, ParamType type, const QVariant & value, bool ro,
  const QString & desc, bool hasRange = false, double lo = 0.0, double hi = 0.0)
{
  ParamInfo p;
  p.name = name;
  p.type = type;
  p.value = value;
  p.readOnly = ro;
  p.description = desc;
  p.hasRange = hasRange;
  p.min = lo;
  p.max = hi;
  return p;
}
}  // namespace

FakeGatewayServer::FakeGatewayServer(QObject * parent)
: QObject(parent)
{
  params_ = {
    makeParam(QStringLiteral("angular.anti_windup"), ParamType::Bool, true, false,
      QStringLiteral("회전 적분 포화 방지")),
    makeParam(QStringLiteral("control_rate_hz"), ParamType::Double, 20.0, true,
      QStringLiteral("제어 주기 [Hz] (시작 시에만)"), true, 1.0, 100.0),
    makeParam(QStringLiteral("linear.kd"), ParamType::Double, 0.0, false,
      QStringLiteral("직선 D 이득"), true, 0.0, 10.0),
    makeParam(QStringLiteral("linear.kp"), ParamType::Double, 0.6, false,
      QStringLiteral("직선 P 이득"), true, 0.0, 10.0),
    makeParam(QStringLiteral("linear.output_limit"), ParamType::Double, 0.22, false,
      QStringLiteral("직선 출력 한도 [m/s]"), true, 0.01, 0.26),
    makeParam(QStringLiteral("odom_topic"), ParamType::String, QStringLiteral("odom"), true,
      QStringLiteral("오도메트리 토픽 (시작 시에만)")),
    makeParam(QStringLiteral("qos_overrides./parameter_events.publisher.depth"), ParamType::Int,
      static_cast<qint64>(1000), true, QString()),
    makeParam(QStringLiteral("use_sim_time"), ParamType::Bool, true, false, QString()),
  };
  connect(&server_, &QTcpServer::newConnection, this, &FakeGatewayServer::onNewConnection);
}

FakeGatewayServer::~FakeGatewayServer()
{
  /* 소켓은 QTcpServer의 자식이라 이 객체의 멤버보다 늦게 지워진다.
   * 그때 disconnected 처리 함수가 이미 사라진 표(buffers_)를 건드리지 않도록 먼저 끊는다. */
  for (QTcpSocket * s : std::as_const(clients_)) {
    s->disconnect(this);
    s->abort();
  }
  qDeleteAll(buffers_);
  buffers_.clear();
  clients_.clear();
  subscribers_.clear();
  server_.close();
}

QList<ParamInfo> FakeGatewayServer::pidNodeParams()
{
  /* pid_goal_controller의 선언과 같은 이름·범위·설명 (화면 캡처·수동 확인용) */
  QList<ParamInfo> v;
  auto pid = [&v](const QString & pre, const QString & label, const QString & unit,
    double kp, double out, double outLo, double outHi, double ilim) {
      v.append(makeParam(pre + QStringLiteral(".anti_windup"), ParamType::Bool, true, false,
        label + QStringLiteral(" 적분 포화 방지 (출력이 한도에 걸린 동안 적분 중지)")));
      v.append(makeParam(pre + QStringLiteral(".d_filter_tau"), ParamType::Double, 0.05, false,
        label + QStringLiteral(" 미분 저역 필터 시정수 [s] (0 = 필터 없음)"), true, 0.0, 1.0));
      v.append(makeParam(pre + QStringLiteral(".integral_limit"), ParamType::Double, ilim, false,
        label + QStringLiteral(" 적분항 최대 기여 [") + unit + QStringLiteral("]"), true, 0.001, 2.0));
      v.append(makeParam(pre + QStringLiteral(".kd"), ParamType::Double, 0.0, false,
        label + QStringLiteral(" D(미분) 이득: 오차 변화에 반응해 흔들림 억제"), true, 0.0, 10.0));
      v.append(makeParam(pre + QStringLiteral(".ki"), ParamType::Double, 0.0, false,
        label + QStringLiteral(" I(적분) 이득: 오래 남은 오차를 없앰"), true, 0.0, 10.0));
      v.append(makeParam(pre + QStringLiteral(".kp"), ParamType::Double, kp, false,
        label + QStringLiteral(" P(비례) 이득: 오차에 비례해 출력"), true, 0.0, 10.0));
      v.append(makeParam(pre + QStringLiteral(".output_limit"), ParamType::Double, out, false,
        label + QStringLiteral(" 출력 한도 [") + unit + QStringLiteral("]"), true, outLo, outHi));
    };
  v.append(makeParam(QStringLiteral("align_final_yaw"), ParamType::Bool, true, false,
    QStringLiteral("도착 후 지정한 방향으로 회전")));
  pid(QStringLiteral("angular"), QStringLiteral("회전(방향→각속도)"), QStringLiteral("rad/s"),
    1.5, 1.0, 0.05, 1.82, 0.3);
  v.append(makeParam(QStringLiteral("cmd_topic"), ParamType::String, QStringLiteral("cmd_vel_in"), true,
    QStringLiteral("속도 명령 발행 토픽 (시작 시에만)")));
  v.append(makeParam(QStringLiteral("control_rate_hz"), ParamType::Double, 20.0, true,
    QStringLiteral("제어 주기 [Hz] (시작 시에만)"), true, 1.0, 100.0));
  v.append(makeParam(QStringLiteral("distance_tolerance"), ParamType::Double, 0.05, false,
    QStringLiteral("도착 판정 거리 [m]"), true, 0.005, 0.5));
  v.append(makeParam(QStringLiteral("goal_topic"), ParamType::String, QStringLiteral("goal_pose"), true,
    QStringLiteral("목표 자세 토픽 (시작 시에만)")));
  v.append(makeParam(QStringLiteral("heading_lock_distance"), ParamType::Double, 0.15, false,
    QStringLiteral("목표까지 이 거리 안에서는 방향 보정 중지 [m]"), true, 0.0, 1.0));
  v.append(makeParam(QStringLiteral("heading_tolerance_to_move"), ParamType::Double, 0.3, false,
    QStringLiteral("방향 오차가 이 안이면 전진 시작 [rad]"), true, 0.05, 1.57));
  pid(QStringLiteral("linear"), QStringLiteral("직선(거리→선속도)"), QStringLiteral("m/s"),
    0.6, 0.22, 0.01, 0.26, 0.1);
  v.append(makeParam(QStringLiteral("obstacle.enabled"), ParamType::Bool, true, false,
    QStringLiteral("LiDAR 장애물 정지·감속 사용")));
  v.append(makeParam(QStringLiteral("obstacle.half_angle"), ParamType::Double, 0.5236, false,
    QStringLiteral("진행 방향 감시 부채꼴 반각 [rad]"), true, 0.05, 1.57));
  v.append(makeParam(QStringLiteral("obstacle.ignore_below"), ParamType::Double, 0.12, false,
    QStringLiteral("이보다 가까운 값은 자기 몸체 반사로 보고 무시 [m]"), true, 0.0, 0.5));
  v.append(makeParam(QStringLiteral("obstacle.release_margin"), ParamType::Double, 0.05, false,
    QStringLiteral("정지 해제 히스테리시스 [m]"), true, 0.0, 0.5));
  v.append(makeParam(QStringLiteral("obstacle.scan_timeout"), ParamType::Double, 0.5, false,
    QStringLiteral("스캔이 이 시간보다 오래되면 이동 차단 [s]"), true, 0.05, 5.0));
  v.append(makeParam(QStringLiteral("obstacle.slow_distance"), ParamType::Double, 0.7, false,
    QStringLiteral("이 거리 안이면 거리에 비례해 감속 [m] (정지 거리보다 커야 함)"), true, 0.1, 3.0));
  v.append(makeParam(QStringLiteral("obstacle.slow_min_scale"), ParamType::Double, 0.3, false,
    QStringLiteral("정지 거리 바로 바깥에서의 속도 배율"), true, 0.0, 1.0));
  v.append(makeParam(QStringLiteral("obstacle.stop_distance"), ParamType::Double, 0.35, false,
    QStringLiteral("이 거리 안에 물체가 있으면 정지 [m] (LiDAR 중심 기준)"), true, 0.1, 2.0));
  v.append(makeParam(QStringLiteral("odom_frame"), ParamType::String, QStringLiteral("odom"), true,
    QStringLiteral("목표 좌표계, RViz Fixed Frame과 같아야 함 (시작 시에만)")));
  v.append(makeParam(QStringLiteral("odom_topic"), ParamType::String, QStringLiteral("odom"), true,
    QStringLiteral("오도메트리 토픽 (시작 시에만)")));
  v.append(makeParam(QStringLiteral("qos_overrides./parameter_events.publisher.depth"), ParamType::Int,
    static_cast<qint64>(1000), true, QString()));
  v.append(makeParam(QStringLiteral("rotate_threshold"), ParamType::Double, 1.0, false,
    QStringLiteral("전진 중 방향 오차가 이보다 크면 다시 제자리 회전 [rad]"), true, 0.1, 3.14));
  v.append(makeParam(QStringLiteral("safety_state_topic"), ParamType::String,
    QStringLiteral("safety/state"), true, QStringLiteral("안전 상태 토픽 (시작 시에만)")));
  v.append(makeParam(QStringLiteral("scan_topic"), ParamType::String, QStringLiteral("scan"), true,
    QStringLiteral("LiDAR 스캔 토픽 (시작 시에만)")));
  v.append(makeParam(QStringLiteral("use_safety_state"), ParamType::Bool, false, true,
    QStringLiteral("안전 상태 연동 사용 (시작 시에만)")));
  v.append(makeParam(QStringLiteral("use_sim_time"), ParamType::Bool, true, false, QString()));
  v.append(makeParam(QStringLiteral("yaw_tolerance"), ParamType::Double, 0.05, false,
    QStringLiteral("최종 방향 판정 오차 [rad]"), true, 0.005, 0.5));
  std::sort(v.begin(), v.end(), [](const ParamInfo & a, const ParamInfo & b) {return a.name < b.name;});
  return v;
}

bool FakeGatewayServer::start(quint16 port)
{
  return server_.listen(QHostAddress::LocalHost, port);
}

void FakeGatewayServer::onNewConnection()
{
  while (server_.hasPendingConnections()) {
    QTcpSocket * s = server_.nextPendingConnection();
    if (clients_.size() >= maxClients_) {
      s->write("{\"v\":1,\"type\":\"evt\",\"evt\":\"server_full\"}\n");
      s->flush();
      s->disconnectFromHost();
      connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
      continue;
    }
    clients_.append(s);
    buffers_.insert(s, new proto::LineBuffer());
    connect(s, &QTcpSocket::readyRead, this, [this, s]() {
        proto::LineBuffer * b = buffers_.value(s, nullptr);
        if (nullptr == b) {
          return;
        }
        const QList<QByteArray> lines = b->append(s->readAll());
        for (const QByteArray & l : lines) {
          handleLine(s, l);
        }
      });
    connect(s, &QTcpSocket::disconnected, this, [this, s]() {
        clients_.removeAll(s);
        subscribers_.remove(s);
        delete buffers_.take(s);
        s->deleteLater();
      });
  }
}

ParamInfo * FakeGatewayServer::find(const QString & name)
{
  for (ParamInfo & p : params_) {
    if (p.name == name) {
      return &p;
    }
  }
  return nullptr;
}

QJsonObject FakeGatewayServer::ok(qint64 id, const QJsonObject & body) const
{
  QJsonObject o = body;
  o.insert(QStringLiteral("v"), 1);
  o.insert(QStringLiteral("id"), id);
  o.insert(QStringLiteral("type"), QStringLiteral("res"));
  o.insert(QStringLiteral("ok"), true);
  return o;
}

QJsonObject FakeGatewayServer::err(qint64 id, const QString & code, const QString & msg) const
{
  QJsonObject o;
  o.insert(QStringLiteral("v"), 1);
  o.insert(QStringLiteral("id"), id);
  o.insert(QStringLiteral("type"), QStringLiteral("res"));
  o.insert(QStringLiteral("ok"), false);
  o.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
    {QStringLiteral("message"), msg}});
  return o;
}

void FakeGatewayServer::writeLine(QTcpSocket * s, const QJsonObject & obj)
{
  QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
  line.append('\n');
  if (true == splitWrites_) {
    /* 한 줄을 둘로 나눠 시간차를 두고 보낸다 (TCP 쪼개짐 흉내) */
    const qsizetype half = line.size() / 2;
    s->write(line.left(half));
    s->flush();
    const QByteArray rest = line.mid(half);
    QTimer::singleShot(20, s, [s, rest]() {s->write(rest);});
  } else {
    s->write(line);
  }
}

void FakeGatewayServer::handleLine(QTcpSocket * s, const QByteArray & line)
{
  ++requestCount_;
  const QJsonObject req = QJsonDocument::fromJson(line).object();
  const qint64 id = req.value(QStringLiteral("id")).toInteger(-1);
  const QString cmd = req.value(QStringLiteral("cmd")).toString();
  lastCmd_ = cmd;
  if ((false == silentCmd_.isEmpty()) && (silentCmd_ == cmd)) {
    return;   // 응답하지 않음 (시간 초과 시험)
  }

  if (QLatin1String("ping") == cmd) {
    writeLine(s, ok(id, QJsonObject{{QStringLiteral("pong"), true}}));
  } else if (QLatin1String("hello") == cmd) {
    writeLine(s, ok(id, QJsonObject{
        {QStringLiteral("gateway"), QStringLiteral("amr_param_gateway")},
        {QStringLiteral("version"), QStringLiteral("0.1.0-fake")},
        {QStringLiteral("protocol"), 1},
        {QStringLiteral("node"), QStringLiteral("/pid_goal_controller")},
        {QStringLiteral("node_ready"), nodeReady_},
        {QStringLiteral("time"), kst::nowIso()}}));
  } else if (QLatin1String("list") == cmd) {
    if (false == nodeReady_) {
      writeLine(s, err(id, QStringLiteral("NODE_NOT_FOUND"), QStringLiteral("target node not available")));
      return;
    }
    QJsonArray arr;
    for (const ParamInfo & p : params_) {
      QJsonObject o{{QStringLiteral("name"), p.name},
        {QStringLiteral("type"), proto::typeToString(p.type)},
        {QStringLiteral("value"), proto::toJson(p.value, p.type)},
        {QStringLiteral("read_only"), p.readOnly},
        {QStringLiteral("description"), p.description}};
      if (true == p.hasRange) {
        o.insert(QStringLiteral("range"), QJsonObject{{QStringLiteral("min"), p.min},
          {QStringLiteral("max"), p.max}, {QStringLiteral("step"), p.step}});
      }
      arr.append(o);
    }
    writeLine(s, ok(id, QJsonObject{{QStringLiteral("node"), QStringLiteral("/pid_goal_controller")},
      {QStringLiteral("params"), arr}}));
  } else if (QLatin1String("set") == cmd) {
    const QJsonObject params = req.value(QStringLiteral("params")).toObject();
    if (true == params.isEmpty()) {
      writeLine(s, err(id, QStringLiteral("BAD_REQUEST"), QStringLiteral("params must be a non-empty object")));
      return;
    }
    /* 1차: 전부 검사 (하나라도 실패하면 아무것도 바꾸지 않음) */
    QHash<QString, QVariant> staged;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
      ParamInfo * p = find(it.key());
      if (nullptr == p) {
        writeLine(s, err(id, QStringLiteral("REJECTED"), QStringLiteral("unknown parameter: ") + it.key()));
        return;
      }
      if (true == p->readOnly) {
        writeLine(s, err(id, QStringLiteral("REJECTED"),
          QStringLiteral("read-only parameter (startup only): ") + it.key()));
        return;
      }
      const QVariant v = proto::fromJson(it.value(), p->type);
      if (false == v.isValid()) {
        writeLine(s, err(id, QStringLiteral("BAD_REQUEST"), it.key() + QStringLiteral(": type mismatch")));
        return;
      }
      if ((true == p->hasRange) && ((v.toDouble() < p->min) || (v.toDouble() > p->max))) {
        writeLine(s, err(id, QStringLiteral("REJECTED"),
          QStringLiteral("Parameter {%1} doesn't comply with floating point range.").arg(it.key())));
        return;
      }
      staged.insert(it.key(), v);
    }
    /* 2차: 적용과 알림 */
    QJsonObject applied;
    for (auto it = staged.cbegin(); it != staged.cend(); ++it) {
      ParamInfo * p = find(it.key());
      p->value = it.value();
      applied.insert(it.key(), proto::toJson(p->value, p->type));
    }
    writeLine(s, ok(id, QJsonObject{{QStringLiteral("applied"), applied}}));
    pushParamChanged(applied);
  } else if (QLatin1String("subscribe") == cmd) {
    subscribers_.insert(s);
    writeLine(s, ok(id, QJsonObject{{QStringLiteral("subscribed"),
      QJsonArray{QStringLiteral("param_changed")}}}));
  } else {
    writeLine(s, err(id, QStringLiteral("BAD_REQUEST"), QStringLiteral("unknown cmd: ") + cmd));
  }
}

void FakeGatewayServer::pushParamChanged(const QJsonObject & params)
{
  for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
    ParamInfo * p = find(it.key());
    if (nullptr != p) {
      const QVariant v = proto::fromJson(it.value(), p->type);
      if (true == v.isValid()) {
        p->value = v;
      }
    }
  }
  const QJsonObject evt{{QStringLiteral("v"), 1}, {QStringLiteral("type"), QStringLiteral("evt")},
    {QStringLiteral("evt"), QStringLiteral("param_changed")},
    {QStringLiteral("node"), QStringLiteral("/pid_goal_controller")},
    {QStringLiteral("params"), params}, {QStringLiteral("stamp"), kst::nowIso()}};
  for (QTcpSocket * s : std::as_const(subscribers_)) {
    writeLine(s, evt);
  }
}

void FakeGatewayServer::dropAll()
{
  const QList<QTcpSocket *> list = clients_;
  for (QTcpSocket * s : list) {
    s->abort();
  }
}

}  // namespace amr::test
