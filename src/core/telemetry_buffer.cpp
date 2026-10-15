/**
 * @file    telemetry_buffer.cpp
 * @brief   그래프 데이터 보관·조회 구현
 */
#include "core/telemetry_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace amr
{

namespace
{

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

/** 채널 키 표 (QML·조건과 맞추는 이름) */
struct KeyEntry
{
  Channel channel;
  const char * key;
};

constexpr std::array<KeyEntry, kChannelCount> kKeys{{
  {Channel::CmdV, "cmd.v"},
  {Channel::CmdW, "cmd.w"},
  {Channel::TrajVRef, "traj.v_ref"},
  {Channel::TrajSRef, "traj.s_ref"},
  {Channel::TrajSAct, "traj.s_act"},
  {Channel::TrajErrMm, "traj.err_mm"},
  {Channel::LinErr, "lin.err"},
  {Channel::LinP, "lin.p"},
  {Channel::LinI, "lin.i"},
  {Channel::LinD, "lin.d"},
  {Channel::LinOut, "lin.out"},
  {Channel::LinSat, "lin.sat"},
  {Channel::AngErr, "ang.err"},
  {Channel::AngP, "ang.p"},
  {Channel::AngI, "ang.i"},
  {Channel::AngD, "ang.d"},
  {Channel::AngOut, "ang.out"},
  {Channel::AngSat, "ang.sat"},
  {Channel::GoalDist, "goal.dist"},
  {Channel::GoalHeadingDeg, "goal.heading_deg"},
  {Channel::GoalYawDeg, "goal.yaw_deg"},
  {Channel::State, "state"},
}};

constexpr std::size_t idx(Channel c)
{
  return static_cast<std::size_t>(c);
}

}  // namespace

bool TelemetryBuffer::channelFromKey(const QString & key, Channel & out)
{
  bool found = false;
  for (const KeyEntry & e : kKeys) {
    if (QLatin1String(e.key) == key) {
      out = e.channel;
      found = true;
      break;
    }
  }
  return found;
}

QString TelemetryBuffer::key(Channel c)
{
  QString k;
  for (const KeyEntry & e : kKeys) {
    if (e.channel == c) {
      k = QString::fromLatin1(e.key);
      break;
    }
  }
  return k;
}

QString TelemetryBuffer::stateName(int state)
{
  QString n;
  switch (state) {
    case 0:
      n = QStringLiteral("정지");
      break;
    case 1:
      n = QStringLiteral("방향 회전");
      break;
    case 2:
      n = QStringLiteral("직진");
      break;
    case 3:
      n = QStringLiteral("최종 회전");
      break;
    case 4:
      n = QStringLiteral("도착");
      break;
    default:
      n = QStringLiteral("?");
      break;
  }
  return n;
}

TelemetryBuffer::TelemetryBuffer(qsizetype capacity)
: capacity_((0 < capacity) ? capacity : 1)
{
}

void TelemetryBuffer::clear()
{
  rows_.clear();
  hasOrigin_ = false;
}

void TelemetryBuffer::startRun(double tAbs)
{
  rows_.clear();
  origin_ = tAbs;
  hasOrigin_ = true;
  lastAbs_ = tAbs;
  ++runNumber_;
}

AppendResult TelemetryBuffer::append(const proto::TelemetrySample & s)
{
  AppendResult result = AppendResult::Added;
  if (false == hasOrigin_) {
    startRun(s.t);
    result = AppendResult::NewRun;
  } else if ((false == rows_.empty()) && (s.t < (lastAbs_ - kTimeJumpBackS))) {
    /* 시뮬레이션을 다시 시작하면 시계가 0 근처로 돌아간다 */
    startRun(s.t);
    result = AppendResult::NewRun;
  } else if (s.t < origin_) {
    /* 목표 알림보다 먼저 찍힌 주기(같은 주기에 목표를 받은 경우 등)는 이전 이동 것이다 */
    return AppendResult::Dropped;
  } else {
    /* 그대로 쌓음 */
  }

  rows_.push_back(toRow(s, s.t - origin_));
  lastAbs_ = s.t;
  while (static_cast<qsizetype>(rows_.size()) > capacity_) {
    rows_.pop_front();
  }
  return result;
}

TelemetryBuffer::Row TelemetryBuffer::toRow(const proto::TelemetrySample & s, double t)
{
  Row r;
  r.t = t;
  r.v.fill(kNaN);

  if (true == s.hasCmd) {
    r.v[idx(Channel::CmdV)] = s.cmdV;
    r.v[idx(Channel::CmdW)] = s.cmdW;
  }
  if (true == s.hasTraj) {
    /* 궤적을 따라가는 중일 때만 기준·오차를 쓴다 (그 밖에는 이전 계획 값이 남아 있음) */
    const bool active = (0.5 <= s.traj[7]);
    if (true == active) {
      r.v[idx(Channel::TrajSRef)] = s.traj[0];
      r.v[idx(Channel::TrajVRef)] = s.traj[1];
      r.v[idx(Channel::TrajSAct)] = s.traj[3];
      r.v[idx(Channel::TrajErrMm)] = s.traj[4] * 1000.0;
    }
  }
  if (true == s.hasLin) {
    r.v[idx(Channel::LinErr)] = s.lin[0];
    r.v[idx(Channel::LinP)] = s.lin[1];
    r.v[idx(Channel::LinI)] = s.lin[2];
    r.v[idx(Channel::LinD)] = s.lin[3];
    r.v[idx(Channel::LinOut)] = s.lin[4];
    r.v[idx(Channel::LinSat)] = s.lin[5];
  }
  if (true == s.hasAng) {
    r.v[idx(Channel::AngErr)] = s.ang[0];
    r.v[idx(Channel::AngP)] = s.ang[1];
    r.v[idx(Channel::AngI)] = s.ang[2];
    r.v[idx(Channel::AngD)] = s.ang[3];
    r.v[idx(Channel::AngOut)] = s.ang[4];
    r.v[idx(Channel::AngSat)] = s.ang[5];
  }
  if (true == s.hasErr) {
    r.v[idx(Channel::GoalDist)] = s.err[0];
    r.v[idx(Channel::GoalHeadingDeg)] = s.err[1] * kRadToDeg;   // NaN은 NaN 그대로
    r.v[idx(Channel::GoalYawDeg)] = s.err[2] * kRadToDeg;
  }
  r.v[idx(Channel::State)] = static_cast<double>(s.state);
  return r;
}

double TelemetryBuffer::firstTime() const
{
  return (true == rows_.empty()) ? 0.0 : rows_.front().t;
}

double TelemetryBuffer::lastTime() const
{
  return (true == rows_.empty()) ? 0.0 : rows_.back().t;
}

double TelemetryBuffer::timeAt(qsizetype i) const
{
  return rows_.at(static_cast<std::size_t>(i)).t;
}

double TelemetryBuffer::value(qsizetype i, Channel c) const
{
  return rows_.at(static_cast<std::size_t>(i)).v[idx(c)];
}

qsizetype TelemetryBuffer::lowerIndex(double t) const
{
  const auto it = std::lower_bound(
    rows_.begin(), rows_.end(), t, [](const Row & r, double x) {return r.t < x;});
  return static_cast<qsizetype>(it - rows_.begin());
}

qsizetype TelemetryBuffer::upperIndex(double t) const
{
  const auto it = std::upper_bound(
    rows_.begin(), rows_.end(), t, [](double x, const Row & r) {return x < r.t;});
  return static_cast<qsizetype>(it - rows_.begin());
}

double TelemetryBuffer::valueAt(Channel c, double t) const
{
  double out = kNaN;
  if (false == std::isnan(t)) {   // NaN과의 비교는 순서가 정해지지 않으므로 먼저 거른다
    const qsizetype i = upperIndex(t) - 1;   // t 이하인 마지막 주기
    if (0 <= i) {
      out = value(i, c);
    }
  }
  return out;
}

double TelemetryBuffer::latest(Channel c) const
{
  return (true == rows_.empty()) ? kNaN : rows_.back().v[idx(c)];
}

QList<QPointF> TelemetryBuffer::points(Channel c, double t0, double t1, qsizetype maxPoints) const
{
  QList<QPointF> out;
  if ((true == rows_.empty()) || (t1 < t0)) {
    return out;
  }
  const std::size_t ci = idx(c);
  const qsizetype n = size();
  const qsizetype begin = std::max<qsizetype>(0, lowerIndex(t0) - 1);
  const qsizetype end = std::min<qsizetype>(n, upperIndex(t1) + 1);
  if (begin >= end) {
    return out;
  }

  const qsizetype limit = std::max<qsizetype>(4, maxPoints);
  if ((end - begin) <= limit) {
    out.reserve(end - begin);
    for (qsizetype i = begin; i < end; ++i) {
      const Row & r = rows_[static_cast<std::size_t>(i)];
      if (false == std::isnan(r.v[ci])) {
        out.append(QPointF(r.t, r.v[ci]));
      }
    }
    return out;
  }

  /* ---- 줄이기: 칸마다 최소·최대 두 점 (시간 순서 유지) ---- */
  const qsizetype buckets = limit / 2;
  const double tb = rows_[static_cast<std::size_t>(begin)].t;
  const double te = rows_[static_cast<std::size_t>(end - 1)].t;
  const double width = (te > tb) ? ((te - tb) / static_cast<double>(buckets)) : 1.0;
  out.reserve(limit);

  qsizetype curBucket = -1;
  qsizetype iMin = -1;
  qsizetype iMax = -1;
  auto flush = [&]() {
      if (0 <= iMin) {
        const Row & a = rows_[static_cast<std::size_t>(std::min(iMin, iMax))];
        const Row & b = rows_[static_cast<std::size_t>(std::max(iMin, iMax))];
        out.append(QPointF(a.t, a.v[ci]));
        if (iMin != iMax) {
          out.append(QPointF(b.t, b.v[ci]));
        }
      }
      iMin = -1;
      iMax = -1;
    };
  for (qsizetype i = begin; i < end; ++i) {
    const Row & r = rows_[static_cast<std::size_t>(i)];
    if (true == std::isnan(r.v[ci])) {
      continue;
    }
    qsizetype b = static_cast<qsizetype>((r.t - tb) / width);
    b = std::min(b, buckets - 1);
    if (b != curBucket) {
      flush();
      curBucket = b;
    }
    if ((0 > iMin) || (r.v[ci] < rows_[static_cast<std::size_t>(iMin)].v[ci])) {
      iMin = i;
    }
    if ((0 > iMax) || (r.v[ci] > rows_[static_cast<std::size_t>(iMax)].v[ci])) {
      iMax = i;
    }
  }
  flush();
  return out;
}

bool TelemetryBuffer::valueRange(Channel c, double t0, double t1, double & lo, double & hi) const
{
  const std::size_t ci = idx(c);
  bool found = false;
  const qsizetype end = upperIndex(t1);
  for (qsizetype i = lowerIndex(t0); i < end; ++i) {
    const double v = rows_[static_cast<std::size_t>(i)].v[ci];
    if (true == std::isnan(v)) {
      continue;
    }
    if (false == found) {
      lo = v;
      hi = v;
      found = true;
    } else {
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
  }
  return found;
}

QList<StateSpan> TelemetryBuffer::stateSpans(double t0, double t1) const
{
  QList<StateSpan> out;
  if ((true == rows_.empty()) || (t1 < t0)) {
    return out;
  }
  const std::size_t si = idx(Channel::State);
  const qsizetype begin = std::max<qsizetype>(0, lowerIndex(t0) - 1);
  const qsizetype end = std::min<qsizetype>(size(), upperIndex(t1) + 1);
  for (qsizetype i = begin; i < end; ++i) {
    const Row & r = rows_[static_cast<std::size_t>(i)];
    const int st = static_cast<int>(r.v[si]);
    /* 구간 끝 = 다음 주기 시각 (마지막 주기는 자기 시각) */
    const double next = ((i + 1) < size()) ? rows_[static_cast<std::size_t>(i + 1)].t : r.t;
    if ((false == out.isEmpty()) && (out.last().state == st)) {
      out.last().t1 = next;
    } else {
      out.append(StateSpan{r.t, next, st});
    }
  }
  return out;
}

double TelemetryBuffer::rateHz(double window) const
{
  if (2U > rows_.size()) {
    return 0.0;
  }
  const double last = rows_.back().t;
  const qsizetype first = lowerIndex(last - window);
  const qsizetype count = size() - first;
  const double span = last - rows_[static_cast<std::size_t>(first)].t;
  return ((1 < count) && (0.0 < span)) ? (static_cast<double>(count - 1) / span) : 0.0;
}

}  // namespace amr
