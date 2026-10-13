/**
 * @file    gateway_protocol.cpp
 * @brief   규약 v1 구현
 */
#include "core/gateway_protocol.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <cmath>
#include <limits>

namespace amr::proto
{

ParamType typeFromString(const QString & s)
{
  ParamType t = ParamType::Unsupported;
  if (QLatin1String("bool") == s) {
    t = ParamType::Bool;
  } else if (QLatin1String("int") == s) {
    t = ParamType::Int;
  } else if (QLatin1String("double") == s) {
    t = ParamType::Double;
  } else if (QLatin1String("string") == s) {
    t = ParamType::String;
  } else {
    /* 모르는 형식은 편집하지 않도록 Unsupported로 둔다 */
  }
  return t;
}

QString typeToString(ParamType t)
{
  QString s;
  switch (t) {
    case ParamType::Bool:
      s = QStringLiteral("bool");
      break;
    case ParamType::Int:
      s = QStringLiteral("int");
      break;
    case ParamType::Double:
      s = QStringLiteral("double");
      break;
    case ParamType::String:
      s = QStringLiteral("string");
      break;
    case ParamType::Unsupported:
    default:
      s = QStringLiteral("unsupported");
      break;
  }
  return s;
}

QByteArray makeRequest(qint64 id, const QString & cmd, const QJsonObject & body)
{
  QJsonObject obj = body;   // 명령별 항목(names, params ...)에 공통 머리를 덧붙인다
  obj.insert(QStringLiteral("v"), kProtocolVersion);
  obj.insert(QStringLiteral("id"), id);
  obj.insert(QStringLiteral("type"), QStringLiteral("req"));
  obj.insert(QStringLiteral("cmd"), cmd);
  QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
  line.append('\n');
  return line;
}

Message parseMessage(const QByteArray & line)
{
  Message m;
  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
  if (QJsonParseError::NoError != perr.error) {
    m.parseError = QStringLiteral("invalid JSON: ") + perr.errorString();
    return m;
  }
  if (false == doc.isObject()) {
    m.parseError = QStringLiteral("not a JSON object");
    return m;
  }
  m.body = doc.object();

  /* 버전 항목이 있으면 1이어야 한다 (없으면 허용: server_full 알림 등) */
  const QJsonValue v = m.body.value(QStringLiteral("v"));
  if ((false == v.isUndefined()) && (kProtocolVersion != v.toInt(-1))) {
    m.parseError = QStringLiteral("unsupported protocol version");
    return m;
  }

  const QString type = m.body.value(QStringLiteral("type")).toString();
  if (QLatin1String("res") == type) {
    const QJsonValue id = m.body.value(QStringLiteral("id"));
    if (false == id.isDouble()) {
      m.parseError = QStringLiteral("response without numeric id");
      return m;
    }
    m.kind = Message::Kind::Response;
    m.id = id.toInteger(-1);
    m.ok = m.body.value(QStringLiteral("ok")).toBool(false);
    if (false == m.ok) {
      const QJsonObject e = m.body.value(QStringLiteral("error")).toObject();
      m.errorCode = e.value(QStringLiteral("code")).toString(QStringLiteral("UNKNOWN"));
      m.errorMessage = e.value(QStringLiteral("message")).toString();
    }
  } else if (QLatin1String("evt") == type) {
    m.kind = Message::Kind::Event;
    m.event = m.body.value(QStringLiteral("evt")).toString();
    if (true == m.event.isEmpty()) {
      m.kind = Message::Kind::Invalid;
      m.parseError = QStringLiteral("event without name");
    }
  } else {
    m.parseError = QStringLiteral("unknown message type: ") + type;
  }
  return m;
}

bool parseParamList(const QJsonObject & body, QList<ParamInfo> & out, QString & err)
{
  out.clear();
  const QJsonValue pv = body.value(QStringLiteral("params"));
  if (false == pv.isArray()) {
    err = QStringLiteral("list response has no params array");
    return false;
  }
  const QJsonArray arr = pv.toArray();
  out.reserve(arr.size());
  for (const QJsonValue & item : arr) {
    const QJsonObject o = item.toObject();
    ParamInfo p;
    p.name = o.value(QStringLiteral("name")).toString();
    if (true == p.name.isEmpty()) {
      err = QStringLiteral("parameter without name");
      out.clear();
      return false;
    }
    p.type = typeFromString(o.value(QStringLiteral("type")).toString());
    if (ParamType::Unsupported != p.type) {
      p.value = fromJson(o.value(QStringLiteral("value")), p.type);
    }
    p.readOnly = o.value(QStringLiteral("read_only")).toBool(false);
    p.description = o.value(QStringLiteral("description")).toString();
    const QJsonValue r = o.value(QStringLiteral("range"));
    if (true == r.isObject()) {
      const QJsonObject ro = r.toObject();
      p.hasRange = true;
      p.min = ro.value(QStringLiteral("min")).toDouble();
      p.max = ro.value(QStringLiteral("max")).toDouble();
      p.step = ro.value(QStringLiteral("step")).toDouble();
    }
    out.append(p);
  }
  return true;
}

QVariant fromJson(const QJsonValue & v, ParamType type)
{
  QVariant out;
  switch (type) {
    case ParamType::Bool:
      if (true == v.isBool()) {
        out = v.toBool();
      }
      break;
    case ParamType::Int:
      /* JSON 숫자는 모두 실수로 읽히므로, 소수부가 없는 값만 정수로 받는다 */
      if (true == v.isDouble()) {
        const double d = v.toDouble();
        if ((std::floor(d) == d) && (std::fabs(d) <= 9.0e15)) {
          out = static_cast<qint64>(d);
        }
      }
      break;
    case ParamType::Double:
      if (true == v.isDouble()) {
        const double d = v.toDouble();
        if (true == std::isfinite(d)) {
          out = d;
        }
      }
      break;
    case ParamType::String:
      if (true == v.isString()) {
        out = v.toString();
      }
      break;
    case ParamType::Unsupported:
    default:
      break;
  }
  return out;
}

QJsonValue toJson(const QVariant & v, ParamType type)
{
  QJsonValue out;
  switch (type) {
    case ParamType::Bool:
      out = v.toBool();
      break;
    case ParamType::Int:
      out = v.toLongLong();
      break;
    case ParamType::Double:
      out = v.toDouble();
      break;
    case ParamType::String:
      out = v.toString();
      break;
    case ParamType::Unsupported:
    default:
      out = QJsonValue(QJsonValue::Null);
      break;
  }
  return out;
}

QString displayValue(const QVariant & v, ParamType type)
{
  QString s;
  if (false == v.isValid()) {
    return s;
  }
  switch (type) {
    case ParamType::Bool:
      s = (true == v.toBool()) ? QStringLiteral("true") : QStringLiteral("false");
      break;
    case ParamType::Int:
      s = QString::number(v.toLongLong());
      break;
    case ParamType::Double:
      /* 유효 숫자 6자리. 정수처럼 보이는 실수는 ".0"을 붙여 형식을 드러낸다 (20 → 20.0) */
      s = QString::number(v.toDouble(), 'g', 6);
      if ((false == s.contains(QLatin1Char('.'))) && (false == s.contains(QLatin1Char('e')))) {
        s.append(QStringLiteral(".0"));
      }
      break;
    case ParamType::String:
      s = v.toString();
      break;
    case ParamType::Unsupported:
    default:
      break;
  }
  return s;
}

bool sameValue(const QVariant & a, const QVariant & b, ParamType type)
{
  if ((false == a.isValid()) || (false == b.isValid())) {
    return a.isValid() == b.isValid();
  }
  bool same = false;
  switch (type) {
    case ParamType::Bool:
      same = (a.toBool() == b.toBool());
      break;
    case ParamType::Int:
      same = (a.toLongLong() == b.toLongLong());
      break;
    case ParamType::Double: {
        /* 화면 입력(문자열→실수)과 JSON 왕복에서 생기는 아주 작은 차이는 같은 값으로 본다 */
        const double x = a.toDouble();
        const double y = b.toDouble();
        const double scale = std::fmax(1.0, std::fmax(std::fabs(x), std::fabs(y)));
        same = (std::fabs(x - y) <= (1.0e-9 * scale));
        break;
      }
    case ParamType::String:
      same = (a.toString() == b.toString());
      break;
    case ParamType::Unsupported:
    default:
      same = true;
      break;
  }
  return same;
}

/* ===========================================================================
 * LineBuffer
 * =========================================================================== */
LineBuffer::LineBuffer(qsizetype maxLine)
: maxLine_(maxLine)
{
}

QList<QByteArray> LineBuffer::append(const QByteArray & data)
{
  QList<QByteArray> lines;
  if (true == overflow_) {
    return lines;
  }
  buffer_.append(data);

  qsizetype start = 0;
  while (true) {
    const qsizetype nl = buffer_.indexOf('\n', start);
    if (0 > nl) {
      break;
    }
    QByteArray line = buffer_.mid(start, nl - start);
    if ((false == line.isEmpty()) && ('\r' == line.back())) {
      line.chop(1);
    }
    if (false == line.isEmpty()) {
      lines.append(line);
    }
    start = nl + 1;
  }
  buffer_.remove(0, start);

  if (buffer_.size() > maxLine_) {
    overflow_ = true;
    buffer_.clear();
  }
  return lines;
}

void LineBuffer::clear()
{
  buffer_.clear();
  overflow_ = false;
}

}  // namespace amr::proto
