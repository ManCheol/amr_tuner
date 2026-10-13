/**
 * @file    condition_file.cpp
 * @brief   시험 조건 파일 구현
 */
#include "core/condition_file.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include "core/kst_time.hpp"

namespace amr
{

namespace
{
constexpr qint64 kMaxFileBytes = 1024 * 1024;   // 조건 파일은 수 KB: 1 MB 넘으면 잘못된 파일
}  // namespace

QByteArray ConditionFile::toJson(const Condition & c)
{
  QJsonObject o;
  o.insert(QStringLiteral("format"), QString::fromLatin1(kFormat));
  o.insert(QStringLiteral("node"), c.node);
  o.insert(QStringLiteral("saved_at"), c.savedAt.isEmpty() ? kst::nowIso() : c.savedAt);
  o.insert(QStringLiteral("note"), c.note);
  o.insert(QStringLiteral("params"), c.params);
  /* 사람이 읽고 git diff로 비교하기 쉽도록 들여쓰기 형식으로 저장 (이름순 정렬은 QJsonObject가 보장) */
  return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

bool ConditionFile::fromJson(const QByteArray & data, Condition & out, QString & err)
{
  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
  if (QJsonParseError::NoError != perr.error) {
    err = QStringLiteral("JSON 형식 오류: %1 (위치 %2)").arg(perr.errorString()).arg(perr.offset);
    return false;
  }
  if (false == doc.isObject()) {
    err = QStringLiteral("JSON 객체가 아닙니다");
    return false;
  }
  const QJsonObject o = doc.object();
  const QString format = o.value(QStringLiteral("format")).toString();
  if (QLatin1String(kFormat) != format) {
    err = QStringLiteral("지원하지 않는 형식입니다: '%1' (필요: %2)").arg(format, QLatin1String(kFormat));
    return false;
  }
  const QJsonValue params = o.value(QStringLiteral("params"));
  if (false == params.isObject()) {
    err = QStringLiteral("params 항목이 없습니다");
    return false;
  }
  out.node = o.value(QStringLiteral("node")).toString();
  out.savedAt = o.value(QStringLiteral("saved_at")).toString();
  out.note = o.value(QStringLiteral("note")).toString();
  out.params = params.toObject();
  return true;
}

bool ConditionFile::save(const QString & path, const Condition & c, QString & err)
{
  QSaveFile f(path);
  if (false == f.open(QIODevice::WriteOnly)) {
    err = f.errorString();
    return false;
  }
  const QByteArray data = toJson(c);
  if (data.size() != f.write(data)) {
    err = f.errorString();
    f.cancelWriting();
    return false;
  }
  if (false == f.commit()) {
    err = f.errorString();
    return false;
  }
  return true;
}

bool ConditionFile::load(const QString & path, Condition & out, QString & err)
{
  QFile f(path);
  if (false == f.open(QIODevice::ReadOnly)) {
    err = f.errorString();
    return false;
  }
  if (f.size() > kMaxFileBytes) {
    err = QStringLiteral("파일이 너무 큽니다 (%1 바이트)").arg(f.size());
    return false;
  }
  return fromJson(f.readAll(), out, err);
}

QString ConditionFile::defaultFileName()
{
  return QStringLiteral("cond_%1.json").arg(kst::nowForFileName());
}

}  // namespace amr
