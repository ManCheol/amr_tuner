/**
 * @file    capture_record.cpp
 * @brief   그래프 화면 저장 기록 구현
 */
#include "core/capture_record.hpp"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <cmath>

#include "core/kst_time.hpp"

namespace amr
{

namespace
{
/** 실수 → JSON (NaN·무한대는 null). 소수 3자리(ms·mm 단위)로 반올림해 읽기 쉽게 */
QJsonValue number3(double v)
{
  QJsonValue out(QJsonValue::Null);
  if (true == std::isfinite(v)) {
    out = std::round(v * 1000.0) / 1000.0;
  }
  return out;
}

bool exists(const QDir & dir, const QString & base)
{
  return (true == QFileInfo::exists(dir.filePath(base + QStringLiteral(".png")))) ||
         (true == QFileInfo::exists(dir.filePath(base + QStringLiteral(".json"))));
}
}  // namespace

QString CaptureRecord::baseName(const QDateTime & utc)
{
  return QString::fromLatin1(kPrefix) + kst::toFileName(utc);
}

QString CaptureRecord::uniqueBase(const QDir & dir, const QString & base)
{
  QString name;
  if (false == exists(dir, base)) {
    name = base;
  } else {
    for (int i = 2; i <= kMaxSuffix; ++i) {
      const QString candidate = QStringLiteral("%1_%2").arg(base).arg(i);
      if (false == exists(dir, candidate)) {
        name = candidate;
        break;
      }
    }
  }
  return name;   // 비었으면 실패 (같은 초에 100번 넘게 저장)
}

QJsonObject CaptureRecord::captureObject(const CaptureInfo & info)
{
  QJsonObject o;
  o.insert(QStringLiteral("image"), info.image);
  o.insert(QStringLiteral("run"), info.run);
  o.insert(QStringLiteral("elapsed_s"), number3(info.elapsedS));
  o.insert(QStringLiteral("view_s"), QJsonArray{number3(info.viewFromS), number3(info.viewToS)});
  o.insert(QStringLiteral("paused"), info.paused);
  o.insert(QStringLiteral("cursor_s"), number3(info.cursorS));
  if (true == info.hasGoal) {
    o.insert(QStringLiteral("goal"), QJsonObject{
      {QStringLiteral("x"), number3(info.goal.x)},
      {QStringLiteral("y"), number3(info.goal.y)},
      {QStringLiteral("yaw"), number3(info.goal.yaw)},
      {QStringLiteral("frame"), info.goal.frame},
      {QStringLiteral("stamp"), info.goal.stamp}});
  } else {
    o.insert(QStringLiteral("goal"), QJsonValue(QJsonValue::Null));
  }
  o.insert(QStringLiteral("pending_changes"), info.pendingChanges);
  o.insert(QStringLiteral("gateway"), info.gateway);
  return o;
}

QByteArray CaptureRecord::toJson(const Condition & c, const CaptureInfo & info)
{
  QJsonObject o = ConditionFile::toObject(c);
  o.insert(QStringLiteral("capture"), captureObject(info));
  return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

bool CaptureRecord::save(const QString & path, const Condition & c, const CaptureInfo & info, QString & err)
{
  return ConditionFile::saveBytes(path, toJson(c, info), err);
}

}  // namespace amr
