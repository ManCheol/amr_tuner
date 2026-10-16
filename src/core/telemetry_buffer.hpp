/**
 * @file    telemetry_buffer.hpp
 * @brief   그래프 데이터 보관·조회 (화면과 무관, 단위 시험 대상)
 *
 * ---------------------------------------------------------------------------
 * [하는 일]
 *   telemetry 알림(한 제어 주기)을 시간순으로 쌓고, 그래프가 필요로 하는 형태로 꺼내 준다.
 *     - points()     : 보이는 시간 구간의 (t, 값) 목록. 점이 많으면 구간별 최소·최대만 남겨 줄인다
 *     - valueAt()    : 커서 시각의 값 (그 시각 이전의 가장 가까운 주기)
 *     - valueRange() : 세로축 범위 자동 맞춤용 최소·최대
 *     - stateSpans() : 상태(회전·직진·도착) 구간 → 그래프 위쪽 색 띠
 *
 * [시간축]
 *   중계 노드 시계(t)를 그대로 쓰지 않고 "이동 시작"을 0초로 둔다.
 *     - startRun(t0) : 목표(goal) 알림을 받았을 때 → 이동 한 번이 0초부터 다시 그려짐
 *     - 처음 받은 주기 : 이동 시작이 정해지지 않았으면 그 시각을 0초로
 *     - 시각이 1초 넘게 거꾸로 가면(시뮬레이션 재시작) 새 이동으로 본다
 *
 * [채널]
 *   한 주기에서 꺼낼 수 있는 값 하나. 화면(QML)은 문자열 키("cmd.v" 등)로 고른다.
 *   궤적 값(traj.*)은 궤적을 따라가는 중(active)일 때만 의미가 있으므로 그 밖에서는 NaN이다.
 *   경로 추종 값(path.*)도 경로를 따라가는 중(active)일 때만 쓴다 (직선 목표·회전 중에는 NaN).
 *   NaN은 그래프에서 점을 찍지 않는다.
 * ---------------------------------------------------------------------------
 */
#ifndef AMR_TUNER_CORE_TELEMETRY_BUFFER_HPP
#define AMR_TUNER_CORE_TELEMETRY_BUFFER_HPP

#include <QList>
#include <QPointF>
#include <QString>

#include <array>
#include <deque>

#include "core/gateway_protocol.hpp"

namespace amr
{

/** 그래프에서 고를 수 있는 값 */
enum class Channel : quint8
{
  CmdV = 0U,        ///< 명령 선속도 [m/s]
  CmdW,             ///< 명령 각속도 [rad/s]
  TrajVRef,         ///< 기준 속도 [m/s] (궤적 추종 중)
  TrajSRef,         ///< 기준 위치 [m]
  TrajSAct,         ///< 실제 위치 [m]
  TrajErrMm,        ///< 궤적 추종 오차 [mm] (+ = 계획보다 뒤처짐)
  LinErr,           ///< 거리 PID 오차 [m]
  LinP,
  LinI,
  LinD,
  LinOut,           ///< 거리 PID 출력 [m/s] (앞먹임 제외)
  LinSat,           ///< 거리 PID 포화 (1/0)
  AngErr,           ///< 방향 PID 오차 [rad]
  AngP,
  AngI,
  AngD,
  AngOut,           ///< 방향 PID 출력 [rad/s]
  AngSat,
  GoalDist,         ///< 목표까지 거리 [m]
  GoalHeadingDeg,   ///< 목표 방향 오차 [°]
  GoalYawDeg,       ///< 최종 자세 오차 [°]
  PathLatMm,        ///< 경로 가로 오차 [mm] (+ = 경로 왼쪽, 경로 추종 중)
  PathHeadingDeg,   ///< 경로 방향 오차 [°]
  PathKappa,        ///< 경로 곡률 [1/m] (+ = 왼쪽으로 휨)
  PathVLimit,       ///< 곡률 속도 상한 [m/s]
  State,            ///< 상태 번호
  Count             ///< 개수 (채널 아님)
};

inline constexpr std::size_t kChannelCount = static_cast<std::size_t>(Channel::Count);

/** 상태 구간 (그래프 색 띠) */
struct StateSpan
{
  double t0{0.0};
  double t1{0.0};
  int state{0};
};

/** append() 결과 */
enum class AppendResult : quint8
{
  Added = 0U,          ///< 쌓음
  NewRun = 1U,         ///< 이동 시작이 정해지지 않았거나 시각이 거꾸로 가서 새 이동으로 시작함
  Dropped = 2U         ///< 이동 시작보다 이전 시각이라 버림
};

class TelemetryBuffer
{
public:
  /** 채널 키 ("cmd.v" 등) ↔ 채널 */
  static bool channelFromKey(const QString & key, Channel & out);
  static QString key(Channel c);

  /** 상태 번호 → 화면 이름 */
  static QString stateName(int state);

  /** 시각이 이만큼 넘게 거꾸로 가면 새 이동으로 본다 [s] */
  static constexpr double kTimeJumpBackS = 1.0;

  /**
   * @param capacity 최대 보관 주기 수. 넘으면 오래된 것부터 버린다.
   *                 기본 12000 = 20 Hz × 10분
   */
  explicit TelemetryBuffer(qsizetype capacity = 12000);

  /** 모두 지우고 이동 시작을 비운다 (다음 주기가 0초) */
  void clear();

  /** 새 이동 시작: 모두 지우고 tAbs(중계 노드 시계)를 0초로 */
  void startRun(double tAbs);

  /** 한 주기 쌓기 */
  AppendResult append(const proto::TelemetrySample & s);

  qsizetype size() const {return static_cast<qsizetype>(rows_.size());}
  bool isEmpty() const {return true == rows_.empty();}
  /** 첫·마지막 주기의 시각 [s, 이동 시작 기준]. 비었으면 0 */
  double firstTime() const;
  double lastTime() const;
  /** 지금까지 시작한 이동 수 (0 = 아직 없음) */
  int runNumber() const {return runNumber_;}
  /** 이동 시작 시각 (중계 노드 시계) */
  double origin() const {return origin_;}
  bool hasOrigin() const {return hasOrigin_;}

  /** i번째 주기의 시각·값 */
  double timeAt(qsizetype i) const;
  double value(qsizetype i, Channel c) const;

  /** 시각 t 이전의 가장 가까운 주기 값. t가 첫 주기보다 앞이거나 비었으면 NaN */
  double valueAt(Channel c, double t) const;
  /** 마지막 주기 값 (비었으면 NaN) */
  double latest(Channel c) const;

  /**
   * [t0, t1] 구간의 점 목록 (NaN 제외). 구간 바로 앞·뒤 한 점씩 포함해 선이 끊기지 않게 한다.
   * 점이 maxPoints보다 많으면 시간을 maxPoints/2개 칸으로 나눠 칸마다 최소·최대만 남긴다
   * (튀는 값이 사라지지 않는 줄이기).
   */
  QList<QPointF> points(Channel c, double t0, double t1, qsizetype maxPoints = 4000) const;

  /** [t0, t1]의 최소·최대 (NaN 제외). 값이 하나도 없으면 false */
  bool valueRange(Channel c, double t0, double t1, double & lo, double & hi) const;

  /** [t0, t1]과 겹치는 상태 구간 (같은 상태가 이어지면 하나로) */
  QList<StateSpan> stateSpans(double t0, double t1) const;

  /** 최근 window초 동안의 수신 주기 [Hz] (표시용) */
  double rateHz(double window = 2.0) const;

private:
  struct Row
  {
    double t{0.0};
    std::array<double, kChannelCount> v{};
  };

  /** 시각 t 이상인 첫 주기 번호 */
  qsizetype lowerIndex(double t) const;
  /** 시각 t보다 큰 첫 주기 번호 */
  qsizetype upperIndex(double t) const;

  static Row toRow(const proto::TelemetrySample & s, double t);

  std::deque<Row> rows_;
  qsizetype capacity_;
  double origin_{0.0};
  bool hasOrigin_{false};
  double lastAbs_{0.0};
  int runNumber_{0};
};

}  // namespace amr

#endif  // AMR_TUNER_CORE_TELEMETRY_BUFFER_HPP
