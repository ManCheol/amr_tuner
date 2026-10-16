/**
 * @file    kst_time.cpp
 * @brief   서울 시간 표기 구현
 */
#include "core/kst_time.hpp"

namespace amr::kst
{

namespace
{
constexpr qint64 kOffsetSec = 9 * 3600;   // UTC+09:00

/** UTC 시각을 서울 벽시계 시각으로 옮긴 값 (시간대 정보 없이 숫자만 사용) */
QDateTime shifted(const QDateTime & utc)
{
  return utc.toUTC().addSecs(kOffsetSec);
}
}  // namespace

QString toIso(const QDateTime & utc)
{
  return shifted(utc).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) + QStringLiteral("+09:00");
}

QString nowIso()
{
  return toIso(QDateTime::currentDateTimeUtc());
}

QString nowForLog()
{
  return shifted(QDateTime::currentDateTimeUtc()).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

QString toFileName(const QDateTime & utc)
{
  return shifted(utc).toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

QString nowForFileName()
{
  return toFileName(QDateTime::currentDateTimeUtc());
}

}  // namespace amr::kst
