/**
 * @file    capture_record.hpp
 * @brief   그래프 화면 저장 기록 (PNG와 같은 이름의 JSON) — 화면과 무관, 단위 시험 대상
 *
 * ---------------------------------------------------------------------------
 * [하는 일]
 *   그래프 탭의 [화면 저장]은 같은 이름으로 두 파일을 남긴다.
 *     graph_20261016_093005.png   그래프 탭 전체 이미지
 *     graph_20261016_093005.json  그 순간의 시험 조건 + 화면 정보
 *   JSON은 시험 조건 파일(amr_tuner/1, condition_file.hpp)에 "capture" 항목을 더한 형식이라
 *   [조건 불러오기]로 그대로 불러와 같은 조건을 다시 시험할 수 있다.
 *
 * [JSON 예]
 *   { "format":"amr_tuner/1", "node":"/pid_goal_controller", "saved_at":"2026-10-16T09:30:05+09:00",
 *     "note":"path_D 3번째 이동, SLAM 보정 직후", "params":{ "linear.kp":5.0, ... },
 *     "capture":{ "image":"graph_20261016_093005.png", "run":3, "elapsed_s":11.85,
 *                 "view_s":[0.0, 11.85], "paused":true, "cursor_s":3.42,
 *                 "goal":{"x":3.41,"y":0.862,"yaw":-1.225,"frame":"map","stamp":"..."},
 *                 "pending_changes":0, "gateway":"0.3.0" } }
 *   - 값이 없으면(데이터 없음, 커서 없음, 목표 전) null
 *   - params는 노드에 적용된 값 (적용하지 않은 변경 예정은 pending_changes 개수로만 남김)
 *
 * [파일 이름]
 *   서울 시간 graph_YYYYMMDD_HHMMSS. 같은 초에 다시 누르면 _2, _3 … (PNG·JSON 어느 쪽이든 있으면 다음 번호)
 * ---------------------------------------------------------------------------
 */
#ifndef AMR_TUNER_CORE_CAPTURE_RECORD_HPP
#define AMR_TUNER_CORE_CAPTURE_RECORD_HPP

#include <QDateTime>
#include <QDir>
#include <QJsonObject>
#include <QString>

#include <limits>

#include "core/condition_file.hpp"
#include "core/gateway_protocol.hpp"

namespace amr
{

/** 화면 저장 순간의 그래프 정보 (그래프 탭이 채움) */
struct CaptureInfo
{
  QString image;                 ///< PNG 파일 이름 (같은 폴더, 경로 없이)
  int run{0};                    ///< 이동 번호 (0 = 아직 없음)
  double elapsedS{std::numeric_limits<double>::quiet_NaN()};   ///< 마지막 데이터 시각 [s, 이동 시작 기준]
  double viewFromS{std::numeric_limits<double>::quiet_NaN()};  ///< 보이는 가로축 시작 [s]
  double viewToS{std::numeric_limits<double>::quiet_NaN()};    ///< 보이는 가로축 끝 [s]
  bool paused{false};            ///< 일시정지 중이었는지
  double cursorS{std::numeric_limits<double>::quiet_NaN()};    ///< 커서 시각 [s] (없으면 NaN)
  bool hasGoal{false};
  proto::GoalInfo goal;          ///< 마지막으로 받은 목표
  int pendingChanges{0};         ///< 적용하지 않은 변경 예정 개수
  QString gateway;               ///< 중계 노드 버전
};

class CaptureRecord
{
public:
  /** 파일 이름 앞부분 */
  static constexpr const char * kPrefix = "graph_";
  /** 같은 이름이 있을 때 붙일 번호의 최대값 (넘으면 빈 문자열 = 실패) */
  static constexpr int kMaxSuffix = 99;

  /** UTC 시각 → "graph_20261016_093005" (서울 시간) */
  static QString baseName(const QDateTime & utc);

  /**
   * dir 안에서 base.png·base.json이 모두 없는 이름. 있으면 base_2, base_3 …
   * kMaxSuffix를 넘으면 빈 문자열.
   */
  static QString uniqueBase(const QDir & dir, const QString & base);

  /** "capture" 항목 */
  static QJsonObject captureObject(const CaptureInfo & info);

  /** 조건 + capture → JSON 문서 (들여쓰기) */
  static QByteArray toJson(const Condition & c, const CaptureInfo & info);

  /** JSON 저장 (임시 파일에 쓴 뒤 바꿔치기) */
  static bool save(const QString & path, const Condition & c, const CaptureInfo & info, QString & err);
};

}  // namespace amr

#endif  // AMR_TUNER_CORE_CAPTURE_RECORD_HPP
