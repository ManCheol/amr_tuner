/**
 * @file    kst_time.hpp
 * @brief   서울 시간(UTC+09:00) 표기 도우미
 *
 * 모든 기록(로그, 조건 파일)은 서울 시간으로 남긴다.
 * PC의 시간대 설정과 관계없이 같은 결과가 나오도록 UTC에 9시간을 더해 만든다.
 * (한국은 일광 절약 시간을 쓰지 않으므로 고정 오프셋으로 충분하다)
 */
#ifndef AMR_TUNER_CORE_KST_TIME_HPP
#define AMR_TUNER_CORE_KST_TIME_HPP

#include <QDateTime>
#include <QString>

namespace amr::kst
{

/** UTC 시각 → "2026-10-14T09:30:00+09:00" (조건 파일·규약용) */
QString toIso(const QDateTime & utc);

/** 현재 시각의 ISO 표기 */
QString nowIso();

/** 현재 시각 "2026-10-14 09:30:00.123" (로그용, 밀리초 포함) */
QString nowForLog();

/** 현재 시각 "20261014_093000" (파일 이름용) */
QString nowForFileName();

}  // namespace amr::kst

#endif  // AMR_TUNER_CORE_KST_TIME_HPP
