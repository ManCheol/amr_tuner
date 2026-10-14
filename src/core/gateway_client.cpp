/**
 * @file    gateway_client.cpp
 * @brief   중계 노드 TCP 클라이언트 구현
 */
#include "core/gateway_client.hpp"

#include <QList>

#include "core/app_log.hpp"

namespace amr
{

namespace
{
constexpr int kTimeoutTickMs = 100;
constexpr int kDefaultReconnectMs = 2000;
constexpr int kDefaultConnectTimeoutMs = 3000;
const QString kTag = QStringLiteral("NET");
}  // namespace

GatewayClient::GatewayClient(QObject * parent)
: QObject(parent)
{
  clock_.start();

  connect(&socket_, &QTcpSocket::connected, this, &GatewayClient::onConnected);
  connect(&socket_, &QTcpSocket::disconnected, this, &GatewayClient::onDisconnected);
  connect(&socket_, &QTcpSocket::readyRead, this, &GatewayClient::onReadyRead);
  connect(&socket_, &QTcpSocket::errorOccurred, this, &GatewayClient::onSocketError);

  timeoutTimer_.setInterval(kTimeoutTickMs);
  connect(&timeoutTimer_, &QTimer::timeout, this, &GatewayClient::onTimeoutTick);

  reconnectTimer_.setSingleShot(true);
  reconnectTimer_.setInterval(kDefaultReconnectMs);
  connect(&reconnectTimer_, &QTimer::timeout, this, &GatewayClient::onReconnectTimer);

  connectTimer_.setSingleShot(true);
  connectTimer_.setInterval(kDefaultConnectTimeoutMs);
  connect(&connectTimer_, &QTimer::timeout, this, &GatewayClient::onConnectTimeout);
}

GatewayClient::~GatewayClient()
{
  /* 소멸 중에는 처리 함수를 부르지 않는다 (처리 함수가 이미 사라진 화면을 가리킬 수 있음) */
  reconnectTimer_.stop();
  connectTimer_.stop();
  timeoutTimer_.stop();
  pending_.clear();
  socket_.disconnect(this);
  socket_.abort();
}

void GatewayClient::setReconnectIntervalMs(int ms)
{
  reconnectTimer_.setInterval(ms);
}

void GatewayClient::setConnectTimeoutMs(int ms)
{
  connectTimer_.setInterval(ms);
}

void GatewayClient::startConnect()
{
  setState(State::Connecting);
  connectTimer_.start();
  socket_.connectToHost(host_, port_);
}

void GatewayClient::connectFailed()
{
  connectTimer_.stop();
  setState(State::Disconnected);
  if ((true == autoReconnect_) && (false == userDisconnect_)) {
    reconnectTimer_.start();
  }
}

void GatewayClient::connectTo(const QString & host, quint16 port, bool autoReconnect)
{
  reconnectTimer_.stop();
  if (QAbstractSocket::UnconnectedState != socket_.state()) {
    /* 이전 연결 정리. abort()는 disconnected 신호를 그 자리에서 보낼 수 있으므로,
     * 사용자 요청으로 표시해 두어 재접속 예약·경고 로그가 생기지 않게 한다. */
    userDisconnect_ = true;
    socket_.abort();
  }
  failAll(QStringLiteral("DISCONNECTED"), QStringLiteral("reconnecting"));

  host_ = host;
  port_ = port;
  autoReconnect_ = autoReconnect;
  userDisconnect_ = false;
  errorLogged_ = false;
  lines_.clear();
  AppLog::info(kTag, QStringLiteral("connecting to %1:%2").arg(host_).arg(port_));
  startConnect();
}

void GatewayClient::disconnectFrom()
{
  userDisconnect_ = true;
  autoReconnect_ = false;
  reconnectTimer_.stop();
  connectTimer_.stop();
  if (QAbstractSocket::UnconnectedState != socket_.state()) {
    AppLog::info(kTag, QStringLiteral("disconnect requested"));
    socket_.abort();
  }
  failAll(QStringLiteral("DISCONNECTED"), QStringLiteral("disconnected by user"));
  setState(State::Disconnected);
}

qint64 GatewayClient::send(const QString & cmd, const QJsonObject & body, ReplyHandler handler)
{
  if (State::Connected != state_) {
    if (handler) {
      Reply r;
      r.code = QStringLiteral("NOT_CONNECTED");
      r.message = QStringLiteral("not connected to gateway");
      handler(r);
    }
    return -1;
  }

  const qint64 id = nextId_;
  ++nextId_;
  Pending p;
  p.cmd = cmd;
  p.handler = std::move(handler);
  p.deadlineMs = clock_.elapsed() + timeoutMs_;
  pending_.insert(id, std::move(p));

  const QByteArray line = proto::makeRequest(id, cmd, body);
  (void)socket_.write(line);
  if (false == timeoutTimer_.isActive()) {
    timeoutTimer_.start();
  }
  return id;
}

void GatewayClient::onConnected()
{
  connectTimer_.stop();
  AppLog::info(kTag, QStringLiteral("connected to %1:%2").arg(host_).arg(port_));
  errorLogged_ = false;
  setState(State::Connected);
}

void GatewayClient::onDisconnected()
{
  const bool wasConnected = (State::Connected == state_);
  connectTimer_.stop();
  failAll(QStringLiteral("DISCONNECTED"), QStringLiteral("connection closed"));
  setState(State::Disconnected);
  if ((true == wasConnected) && (false == userDisconnect_)) {
    AppLog::warn(kTag, QStringLiteral("connection closed by gateway"));
  }
  if ((true == autoReconnect_) && (false == userDisconnect_)) {
    reconnectTimer_.start();
  }
}

void GatewayClient::onSocketError(QAbstractSocket::SocketError err)
{
  /* 원격 종료는 onDisconnected에서 처리하므로 여기서는 접속 실패만 다룬다 */
  if (QAbstractSocket::RemoteHostClosedError == err) {
    return;
  }
  /* 재접속을 반복하는 동안 같은 오류가 2초마다 쌓이지 않도록, 연결될 때까지 한 번만 남긴다 */
  if (false == errorLogged_) {
    AppLog::warn(kTag, QStringLiteral("socket error: %1%2").arg(
        socket_.errorString(),
        (true == autoReconnect_) ? QStringLiteral(" (retrying)") : QString()));
    errorLogged_ = true;
  }
  if (State::Connecting == state_) {
    /* 접속 실패: disconnected 신호가 오지 않을 수 있으므로 여기서 상태를 정리한다 */
    connectFailed();
  }
}

void GatewayClient::onConnectTimeout()
{
  if (State::Connecting != state_) {
    return;   // 그사이 연결됐거나 실패 처리됨
  }
  if (false == errorLogged_) {
    AppLog::warn(kTag, QStringLiteral("connect timeout after %1 ms (%2:%3)%4").arg(
        QString::number(connectTimer_.interval()), host_, QString::number(port_),
        (true == autoReconnect_) ? QStringLiteral(" (retrying)") : QString()));
    errorLogged_ = true;
  }
  /* 상태를 먼저 정리한 뒤 소켓을 끊는다. 접속 중 abort()는 disconnected·errorOccurred를
   * 보내지 않는 것이 보통이지만, 보내더라도 이미 Disconnected라 중복 처리되지 않는다. */
  connectFailed();
  socket_.abort();
}

void GatewayClient::onReconnectTimer()
{
  if ((true == autoReconnect_) && (false == userDisconnect_) && (State::Disconnected == state_)) {
    lines_.clear();
    startConnect();
  }
}

void GatewayClient::onReadyRead()
{
  const QList<QByteArray> lines = lines_.append(socket_.readAll());
  for (const QByteArray & line : lines) {
    handleLine(line);
  }
  if (true == lines_.overflow()) {
    AppLog::error(kTag, QStringLiteral("line too long from gateway: closing connection"));
    socket_.abort();
  }
}

void GatewayClient::handleLine(const QByteArray & line)
{
  const proto::Message m = proto::parseMessage(line);
  if (proto::Message::Kind::Response == m.kind) {
    const auto it = pending_.find(m.id);
    if (pending_.end() == it) {
      /* 이미 시간 초과로 처리한 요청의 늦은 응답 */
      AppLog::warn(kTag, QStringLiteral("late or unknown response id=%1").arg(m.id));
      return;
    }
    Pending p = std::move(it.value());
    pending_.erase(it);
    if (true == pending_.isEmpty()) {
      timeoutTimer_.stop();
    }
    Reply r;
    r.ok = m.ok;
    r.code = m.errorCode;
    r.message = m.errorMessage;
    r.body = m.body;
    if (p.handler) {
      p.handler(r);
    }
  } else if (proto::Message::Kind::Event == m.kind) {
    emit eventReceived(m.event, m.body);
  } else {
    AppLog::warn(kTag, QStringLiteral("ignored message: %1").arg(m.parseError));
  }
}

void GatewayClient::onTimeoutTick()
{
  const qint64 now = clock_.elapsed();
  /* 처리 함수 안에서 새 요청을 보낼 수 있으므로, 먼저 꺼낸 뒤 부른다 */
  QList<Pending> expired;
  for (auto it = pending_.begin(); it != pending_.end(); ) {
    if (now >= it.value().deadlineMs) {
      AppLog::warn(kTag, QStringLiteral("request id=%1 (%2) timed out").arg(it.key()).arg(it.value().cmd));
      expired.append(std::move(it.value()));
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
  if (true == pending_.isEmpty()) {
    timeoutTimer_.stop();
  }
  for (const Pending & p : expired) {
    if (p.handler) {
      Reply r;
      r.code = QStringLiteral("CLIENT_TIMEOUT");
      r.message = QStringLiteral("no response from gateway in %1 ms").arg(timeoutMs_);
      p.handler(r);
    }
  }
}

void GatewayClient::failAll(const QString & code, const QString & message)
{
  if (true == pending_.isEmpty()) {
    return;
  }
  QHash<qint64, Pending> taken;
  taken.swap(pending_);   // 처리 함수가 새 요청을 넣어도 안전하도록 먼저 비운다
  timeoutTimer_.stop();
  for (auto it = taken.cbegin(); it != taken.cend(); ++it) {
    if (it.value().handler) {
      Reply r;
      r.code = code;
      r.message = message;
      it.value().handler(r);
    }
  }
}

void GatewayClient::setState(State s)
{
  if (s != state_) {
    state_ = s;
    emit stateChanged(s);
  }
}

}  // namespace amr
