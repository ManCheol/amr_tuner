/**
 * @file    condition_file.hpp
 * @brief   시험 조건 파일 (PID 설정값 묶음) 저장·불러오기
 *
 * [형식] amr_tuner/1 (JSON, UTF-8)
 *   {
 *     "format":   "amr_tuner/1",
 *     "node":     "/pid_goal_controller",
 *     "saved_at": "2026-10-14T09:30:00+09:00",     ← 서울 시간
 *     "note":     "kp05_kd20 시험 조건",
 *     "params":   { "linear.kp": 0.5, "linear.kd": 2.0, ... }
 *   }
 *
 * [쓰임]
 *   rosbag 기록과 같은 이름으로 저장해 두면, 나중에 그래프를 다시 볼 때
 *   어떤 설정이었는지 알 수 있고, 불러와서 같은 조건으로 다시 시험할 수 있다.
 *   불러온 값은 바로 적용하지 않고 "변경 예정"으로만 넣는다 (화면에서 확인 후 적용).
 */
#ifndef AMR_TUNER_CORE_CONDITION_FILE_HPP
#define AMR_TUNER_CORE_CONDITION_FILE_HPP

#include <QJsonObject>
#include <QString>

namespace amr
{

struct Condition
{
  QString node;
  QString savedAt;     ///< 서울 시간 ISO 표기
  QString note;        ///< 사람이 쓴 메모
  QJsonObject params;  ///< 이름 → 값
};

class ConditionFile
{
public:
  static constexpr const char * kFormat = "amr_tuner/1";

  /** 조건 → JSON 문서 (saved_at이 비어 있으면 현재 서울 시간으로 채움) */
  static QByteArray toJson(const Condition & c);
  /** JSON 문서 → 조건. 형식이 틀리면 false와 사유 */
  static bool fromJson(const QByteArray & data, Condition & out, QString & err);

  /** 파일로 저장 (임시 파일에 쓴 뒤 바꿔치기: 저장 도중 꺼져도 기존 파일이 깨지지 않음) */
  static bool save(const QString & path, const Condition & c, QString & err);
  static bool load(const QString & path, Condition & out, QString & err);

  /** 기본 파일 이름 "cond_20261014_093000.json" */
  static QString defaultFileName();
};

}  // namespace amr

#endif  // AMR_TUNER_CORE_CONDITION_FILE_HPP
