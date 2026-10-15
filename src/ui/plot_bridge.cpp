/**
 * @file    plot_bridge.cpp
 * @brief   그래프 화면과 데이터 버퍼 연결 구현
 */
#include "ui/plot_bridge.hpp"

#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/app_log.hpp"

namespace amr
{

namespace
{

const QString kTagPlot = QStringLiteral("PLOT");

constexpr int kRefreshMs = 50;              // 다시 그리기 주기 (20 Hz)
constexpr qsizetype kMaxPointsPerLine = 2000;   // 선 하나의 최대 점 수 (화면 폭의 약 2배)
constexpr double kMinSpanS = 0.2;           // 가로 확대 한계 [s]
constexpr double kMinWholeRunS = 5.0;       // "이동 전체" 보기의 최소 폭 [s]
constexpr double kPadRatio = 0.08;          // 세로축 위아래 여백 비율
constexpr double kShrinkRate = 0.1;         // 실시간에서 세로축을 좁히는 속도 (갱신마다 차이의 10%)
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/** 선 하나의 정의 */
struct SeriesDef
{
  const char * key;
  const char * name;
  const char * color;
  double width;
};

/** 그래프 하나의 정의 */
struct PanelDef
{
  const char * id;
  const char * title;
  const char * unit;
  int decimals;           ///< 범례 값 소수 자리
  double minSpan;         ///< 세로축 최소 폭 (잡음만 있을 때 과하게 확대되지 않게)
  bool includeZero;       ///< 세로축에 0을 항상 포함
  const char * satKey;    ///< 포화 표시 채널 (없으면 빈 문자열)
  QList<SeriesDef> series;
};

/**
 * 그래프 배치: QML GridLayout(2열)이 이 순서로 왼쪽→오른쪽, 위→아래로 놓는다.
 *   선속도         | 거리 PID 항
 *   궤적 추종 오차  | 방향 PID 항
 *   목표 거리 오차  | 방향 오차
 * 색은 어두운 배경이 아닌 흰 바탕 기준, 같은 그래프 안에서 구별되도록 고름.
 */
const QList<PanelDef> & panelDefs()
{
  static const QList<PanelDef> kDefs{
    {"speed", "선속도", "m/s", 3, 0.05, true, "",
      {{"traj.v_ref", "기준", "#8c959f", 2.0}, {"cmd.v", "명령", "#0969da", 1.5}}},
    {"pidlin", "거리 PID 항", "m/s", 3, 0.02, true, "lin.sat",
      {{"lin.p", "P", "#0969da", 1.5}, {"lin.i", "I", "#bf8700", 1.5},
        {"lin.d", "D", "#8250df", 1.5}, {"lin.out", "출력", "#24292f", 1.5}}},
    {"track", "궤적 추종 오차", "mm", 1, 2.0, true, "",
      {{"traj.err_mm", "추종 오차", "#cf222e", 1.5}}},
    {"pidang", "방향 PID 항", "rad/s", 3, 0.05, true, "ang.sat",
      {{"ang.p", "P", "#0969da", 1.5}, {"ang.i", "I", "#bf8700", 1.5},
        {"ang.d", "D", "#8250df", 1.5}, {"ang.out", "출력", "#24292f", 1.5}}},
    {"dist", "목표 거리 오차", "m", 3, 0.05, true, "",
      {{"goal.dist", "거리", "#1a7f37", 1.5}}},
    {"heading", "방향 오차", "°", 2, 2.0, true, "",
      {{"goal.heading_deg", "목표 방향", "#cf222e", 1.5}, {"goal.yaw_deg", "최종 자세", "#8250df", 1.5}}},
  };
  return kDefs;
}

const PanelDef * findPanel(const QString & id)
{
  const PanelDef * found = nullptr;
  for (const PanelDef & d : panelDefs()) {
    if (QLatin1String(d.id) == id) {
      found = &d;
      break;
    }
  }
  return found;
}

}  // namespace

PlotBridge::PlotBridge(TelemetryBuffer & buffer, QObject * parent)
: QObject(parent),
  buffer_(buffer),
  cursorTime_(kNaN)
{
  refreshTimer_.setInterval(kRefreshMs);
  connect(&refreshTimer_, &QTimer::timeout, this, &PlotBridge::refresh);
  refreshTimer_.start();
}

PlotBridge::~PlotBridge() = default;

QVariantList PlotBridge::panels() const
{
  QVariantList out;
  for (const PanelDef & d : panelDefs()) {
    QVariantList series;
    for (const SeriesDef & s : d.series) {
      series.append(QVariantMap{
        {QStringLiteral("key"), QString::fromLatin1(s.key)},
        {QStringLiteral("name"), QString::fromUtf8(s.name)},
        {QStringLiteral("color"), QColor(QString::fromLatin1(s.color))},
        {QStringLiteral("width"), s.width}});
    }
    out.append(QVariantMap{
      {QStringLiteral("id"), QString::fromLatin1(d.id)},
      {QStringLiteral("title"), QString::fromUtf8(d.title)},
      {QStringLiteral("unit"), QString::fromUtf8(d.unit)},
      {QStringLiteral("decimals"), d.decimals},
      {QStringLiteral("satKey"), QString::fromLatin1(d.satKey)},
      {QStringLiteral("series"), series}});
  }
  return out;
}

QColor PlotBridge::stateColor(int state)
{
  QColor c(QStringLiteral("#afb8c1"));   // 0 정지, 그 밖
  switch (state) {
    case 1:
      c = QColor(QStringLiteral("#fb8f44"));   // 방향 회전
      break;
    case 2:
      c = QColor(QStringLiteral("#54aeff"));   // 직진
      break;
    case 3:
      c = QColor(QStringLiteral("#c297ff"));   // 최종 회전
      break;
    case 4:
      c = QColor(QStringLiteral("#4ac26b"));   // 도착
      break;
    default:
      break;
  }
  return c;
}

QVariantList PlotBridge::stateLegend() const
{
  QVariantList out;
  for (int st = 0; st <= 4; ++st) {
    out.append(QVariantMap{
      {QStringLiteral("state"), st},
      {QStringLiteral("name"), TelemetryBuffer::stateName(st)},
      {QStringLiteral("color"), stateColor(st)}});
  }
  return out;
}

void PlotBridge::setPaused(bool on)
{
  if (paused_ == on) {
    return;
  }
  paused_ = on;
  if (false == on) {
    setCursorTime(kNaN);   // 실시간으로 돌아가면 커서는 의미 없음
  }
  dirty_ = true;
  emit pausedChanged(paused_);
}

void PlotBridge::setCursorTime(double t)
{
  const bool bothNaN = (true == std::isnan(t)) && (true == std::isnan(cursorTime_));
  if ((true == bothNaN) || (t == cursorTime_)) {
    return;
  }
  cursorTime_ = t;
  emit cursorChanged();
}

void PlotBridge::setWindowSeconds(double s)
{
  windowS_ = std::max(0.0, s);
  dirty_ = true;
}

void PlotBridge::resetView()
{
  for (auto it = panels_.begin(); it != panels_.end(); ++it) {
    it->hasRange = false;
  }
  setCursorTime(kNaN);
  dirty_ = true;
}

void PlotBridge::registerSeries(const QString & panelId, const QString & key, QObject * series)
{
  Channel ch = Channel::CmdV;
  auto * xy = qobject_cast<QXYSeries *>(series);
  if (false == TelemetryBuffer::channelFromKey(key, ch)) {
    AppLog::error(kTagPlot, QStringLiteral("unknown channel key: %1").arg(key));
  } else if (nullptr == xy) {
    AppLog::error(kTagPlot, QStringLiteral("%1/%2 is not an XY series").arg(panelId, key));
  } else {
    panels_[panelId].series.append(SeriesRef{key, ch, QPointer<QXYSeries>(xy)});
    dirty_ = true;
  }
}

void PlotBridge::registerPanel(const QString & panelId, QObject * axisX, QObject * axisY)
{
  auto * ax = qobject_cast<QValueAxis *>(axisX);
  auto * ay = qobject_cast<QValueAxis *>(axisY);
  if ((nullptr == ax) || (nullptr == ay)) {
    AppLog::error(kTagPlot, QStringLiteral("%1: axes are not ValueAxis").arg(panelId));
    return;
  }
  PanelRef & p = panels_[panelId];
  p.axisX = ax;
  p.axisY = ay;
  dirty_ = true;
}

double PlotBridge::valueAt(const QString & key) const
{
  Channel ch = Channel::CmdV;
  double v = kNaN;
  if (true == TelemetryBuffer::channelFromKey(key, ch)) {
    if ((true == paused_) && (false == std::isnan(cursorTime_))) {
      v = buffer_.valueAt(ch, cursorTime_);
    } else {
      v = buffer_.latest(ch);
    }
  }
  return v;
}

void PlotBridge::zoomAt(double t, double factor)
{
  if ((false == paused_) || (false == std::isfinite(t)) || (0.0 >= factor)) {
    return;
  }
  const double lo = t - ((t - xMin_) * factor);
  const double hi = t + ((xMax_ - t) * factor);
  setRange(lo, hi);
}

void PlotBridge::panBy(double dt)
{
  if ((false == paused_) || (false == std::isfinite(dt))) {
    return;
  }
  setRange(xMin_ + dt, xMax_ + dt);
}

void PlotBridge::fitAll()
{
  setPaused(true);
  const double first = buffer_.firstTime();
  const double last = std::max(buffer_.lastTime(), first + kMinSpanS);
  setRange(first, last);
  for (auto it = panels_.begin(); it != panels_.end(); ++it) {
    it->hasRange = false;   // 세로축도 전체에 맞춰 다시 계산
  }
}

void PlotBridge::setRange(double lo, double hi)
{
  if (kMinSpanS > (hi - lo)) {
    const double mid = 0.5 * (lo + hi);
    lo = mid - (0.5 * kMinSpanS);
    hi = mid + (0.5 * kMinSpanS);
  }
  if ((lo != xMin_) || (hi != xMax_)) {
    xMin_ = lo;
    xMax_ = hi;
    dirty_ = true;
    emit viewChanged();
  }
}

void PlotBridge::updateLiveRange()
{
  const double first = buffer_.firstTime();
  const double last = buffer_.lastTime();
  double lo = 0.0;
  double hi = 0.0;
  if (0.0 >= windowS_) {
    /* 이동 전체: 이동이 짧을 때는 최소 폭으로 왼쪽 정렬 */
    lo = first;
    hi = std::max(last, first + kMinWholeRunS);
  } else if ((last - first) <= windowS_) {
    /* 창보다 짧은 이동: 0초부터 왼쪽 정렬 (그래프가 왼쪽에서 오른쪽으로 자라남) */
    lo = first;
    hi = first + windowS_;
  } else {
    lo = last - windowS_;
    hi = last;
  }
  setRange(lo, hi);
}

void PlotBridge::updatePanelAxis(const QString & id, PanelRef & p)
{
  const PanelDef * def = findPanel(id);
  if ((nullptr == def) || (nullptr == p.axisY)) {
    return;
  }

  /* ---- 1. 보이는 값의 최소·최대 ---- */
  bool any = false;
  double lo = 0.0;
  double hi = 0.0;
  for (const SeriesRef & s : p.series) {
    double a = 0.0;
    double b = 0.0;
    if (true == buffer_.valueRange(s.channel, xMin_, xMax_, a, b)) {
      lo = (true == any) ? std::min(lo, a) : a;
      hi = (true == any) ? std::max(hi, b) : b;
      any = true;
    }
  }
  if (false == any) {
    lo = 0.0;
    hi = 0.0;
  }
  if (true == def->includeZero) {
    lo = std::min(lo, 0.0);
    hi = std::max(hi, 0.0);
  }

  /* ---- 2. 최소 폭과 여백 ---- */
  if (def->minSpan > (hi - lo)) {
    /* 값이 모두 0 이상(거리 등)이면 0을 바닥에, 모두 0 이하면 0을 천장에 두고 넓힌다 */
    if ((true == def->includeZero) && (0.0 <= lo)) {
      lo = 0.0;
      hi = def->minSpan;
    } else if ((true == def->includeZero) && (0.0 >= hi)) {
      lo = -def->minSpan;
      hi = 0.0;
    } else {
      const double mid = 0.5 * (lo + hi);
      lo = mid - (0.5 * def->minSpan);
      hi = mid + (0.5 * def->minSpan);
    }
  }
  const double pad = (hi - lo) * kPadRatio;
  lo -= pad;
  hi += pad;

  /* ---- 3. 실시간: 넓힐 때는 바로, 좁힐 때는 천천히 ---- */
  if ((false == paused_) && (true == p.hasRange)) {
    lo = (lo < p.lo) ? lo : (p.lo + ((lo - p.lo) * kShrinkRate));
    hi = (hi > p.hi) ? hi : (p.hi + ((hi - p.hi) * kShrinkRate));
  }
  p.lo = lo;
  p.hi = hi;
  p.hasRange = true;
  p.axisY->setRange(lo, hi);
}

void PlotBridge::refresh()
{
  if (false == dirty_) {
    return;
  }
  dirty_ = false;

  if (false == paused_) {
    updateLiveRange();
  }

  for (auto it = panels_.begin(); it != panels_.end(); ++it) {
    PanelRef & p = it.value();
    for (const SeriesRef & s : p.series) {
      if (nullptr != s.series) {
        s.series->replace(buffer_.points(s.channel, xMin_, xMax_, kMaxPointsPerLine));
      }
    }
    if (nullptr != p.axisX) {
      p.axisX->setRange(xMin_, xMax_);
    }
    updatePanelAxis(it.key(), p);
  }

  /* 상태 띠 */
  stateSpans_.clear();
  for (const StateSpan & sp : buffer_.stateSpans(xMin_, xMax_)) {
    stateSpans_.append(QVariantMap{
      {QStringLiteral("t0"), sp.t0},
      {QStringLiteral("t1"), sp.t1},
      {QStringLiteral("state"), sp.state},
      {QStringLiteral("color"), stateColor(sp.state)}});
  }
  ++revision_;
  emit revisionChanged();
}

}  // namespace amr
