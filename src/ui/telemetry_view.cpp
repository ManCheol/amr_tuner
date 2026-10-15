/**
 * @file    telemetry_view.cpp
 * @brief   그래프 탭 구현
 */
#include "ui/telemetry_view.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QQmlError>
#include <QQuickWidget>
#include <QSettings>
#include <QShortcut>
#include <QVBoxLayout>

#include "core/app_log.hpp"
#include "core/gateway_protocol.hpp"

namespace amr
{

namespace
{
const QString kTagPlot = QStringLiteral("PLOT");

constexpr int kStatusMs = 500;              // 상태 글 갱신 주기
constexpr std::size_t kMaxQueued = 12000U;  // 일시정지 대기열 한도 (20 Hz × 10분)
constexpr double kDefaultWindowS = 20.0;

/** 보기 창 선택지 [s] (0 = 이동 전체) */
constexpr double kWindows[] = {10.0, 20.0, 30.0, 60.0, 0.0};
}  // namespace

TelemetryView::TelemetryView(QWidget * parent)
: QWidget(parent),
  bridge_(buffer_)
{
  buildUi();
  loadQml();

  statusTimer_.setInterval(kStatusMs);
  connect(&statusTimer_, &QTimer::timeout, this, &TelemetryView::updateStatus);
  statusTimer_.start();
  connect(&bridge_, &PlotBridge::pausedChanged, this, [this](bool on) {
      /* QML(그래프 클릭·두 번 클릭)에서 일시정지된 경우 버튼도 맞춘다 */
      if (pauseBtn_->isChecked() != on) {
        pauseBtn_->setChecked(on);   // → onPauseToggled
      }
    });
  updateStatus();
}

TelemetryView::~TelemetryView()
{
  /* QML이 먼저 사라지면서 bridge_를 부르지 않도록 QML 화면을 먼저 정리한다 */
  delete quick_;
  quick_ = nullptr;
}

void TelemetryView::buildUi()
{
  auto * root = new QVBoxLayout(this);
  root->setContentsMargins(0, 4, 0, 0);
  root->setSpacing(4);

  auto * bar = new QHBoxLayout();
  pauseBtn_ = new QPushButton(tr("일시정지"), this);
  pauseBtn_->setCheckable(true);
  pauseBtn_->setToolTip(tr("그래프를 멈추고 커서로 값을 봅니다 (Space)\n"
    "멈춘 동안: 휠 = 확대/축소, 끌기 = 이동, 두 번 클릭 = 이동 전체\n"
    "그래프를 클릭해도 멈춥니다"));
  bar->addWidget(pauseBtn_);

  bar->addSpacing(8);
  bar->addWidget(new QLabel(tr("보기"), this));
  windowCombo_ = new QComboBox(this);
  for (const double w : kWindows) {
    windowCombo_->addItem((0.0 < w) ? tr("최근 %1초").arg(w) : tr("이동 전체"), w);
  }
  windowCombo_->setToolTip(tr("실시간 보기에서 가로축에 보일 시간"));
  bar->addWidget(windowCombo_);

  autoStartChk_ = new QCheckBox(tr("목표를 받으면 새로 그리기"), this);
  autoStartChk_->setChecked(true);
  autoStartChk_->setToolTip(tr("새 목표(goal_pose)를 받으면 그래프를 지우고 0초부터 다시 그립니다\n"
    "끄면 이동이 이어서 그려집니다"));
  bar->addWidget(autoStartChk_);

  clearBtn_ = new QPushButton(tr("지우기"), this);
  clearBtn_->setToolTip(tr("그래프를 지우고 다음 데이터부터 0초로 그립니다"));
  bar->addWidget(clearBtn_);

  bar->addStretch(1);
  statusLabel_ = new QLabel(this);
  bar->addWidget(statusLabel_);
  root->addLayout(bar);

  quick_ = new QQuickWidget(this);
  quick_->setResizeMode(QQuickWidget::SizeRootObjectToView);
  quick_->setMinimumHeight(360);
  root->addWidget(quick_, 1);

  errorLabel_ = new QLabel(this);
  errorLabel_->setWordWrap(true);
  errorLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  errorLabel_->setStyleSheet(QStringLiteral("color:#cf222e;"));
  errorLabel_->hide();
  root->addWidget(errorLabel_);

  connect(pauseBtn_, &QPushButton::toggled, this, &TelemetryView::onPauseToggled);
  connect(windowCombo_, &QComboBox::currentIndexChanged, this, &TelemetryView::onWindowChanged);
  connect(clearBtn_, &QPushButton::clicked, this, &TelemetryView::clearPlots);

  /* Space: 그래프 탭 안에 초점이 있을 때만 (표 편집 중의 공백 입력과 겹치지 않게) */
  auto * sc = new QShortcut(QKeySequence(Qt::Key_Space), this);
  sc->setContext(Qt::WidgetWithChildrenShortcut);
  connect(sc, &QShortcut::activated, pauseBtn_, &QPushButton::toggle);
}

void TelemetryView::loadQml()
{
  /* QML에 bridge를 넘긴다 (TelemetryView.qml의 required property bridge) */
  quick_->setInitialProperties({{QStringLiteral("bridge"),
    QVariant::fromValue(static_cast<QObject *>(&bridge_))}});
  quick_->loadFromModule("AmrTuner", "TelemetryView");

  if (QQuickWidget::Error == quick_->status()) {
    QStringList lines;
    for (const QQmlError & e : quick_->errors()) {
      lines.append(e.toString());
      AppLog::error(kTagPlot, QStringLiteral("QML: %1").arg(e.toString()));
    }
    errorLabel_->setText(tr("그래프 화면을 불러오지 못했습니다 (Qt Graphs 설치 확인):\n%1")
      .arg(lines.join(QLatin1Char('\n'))));
    errorLabel_->show();
  } else {
    AppLog::info(kTagPlot, QStringLiteral("graph view ready"));
  }
}

void TelemetryView::loadSettings()
{
  QSettings s;
  const double w = s.value(QStringLiteral("plot/window"), kDefaultWindowS).toDouble();
  int index = windowCombo_->findData(w);
  if (0 > index) {
    index = windowCombo_->findData(kDefaultWindowS);
  }
  windowCombo_->setCurrentIndex(index);
  onWindowChanged(index);   // 같은 번호면 신호가 나가지 않으므로 직접 반영
  autoStartChk_->setChecked(s.value(QStringLiteral("plot/autoStart"), true).toBool());
}

void TelemetryView::saveSettings() const
{
  QSettings s;
  s.setValue(QStringLiteral("plot/window"), windowCombo_->currentData().toDouble());
  s.setValue(QStringLiteral("plot/autoStart"), autoStartChk_->isChecked());
}

/* ===========================================================================
 * 데이터
 * =========================================================================== */
void TelemetryView::onTelemetry(const QJsonObject & body)
{
  if (true == bridge_.paused()) {
    queue_.push_back(Queued{false, body});
    while (kMaxQueued < queue_.size()) {
      queue_.pop_front();
      ++droppedQueued_;
    }
    return;
  }
  applyTelemetry(body);
}

void TelemetryView::onGoal(const QJsonObject & body)
{
  if (true == bridge_.paused()) {
    queue_.push_back(Queued{true, body});
    return;
  }
  applyGoal(body);
}

void TelemetryView::applyTelemetry(const QJsonObject & body)
{
  proto::TelemetrySample s;
  QString err;
  if (false == proto::parseTelemetry(body, s, err)) {
    if (false == badLineLogged_) {   // 20 Hz로 같은 오류가 반복되지 않게 한 번만
      AppLog::warn(kTagPlot, QStringLiteral("bad telemetry (ignored): %1").arg(err));
      badLineLogged_ = true;
    }
    return;
  }
  const AppendResult r = buffer_.append(s);
  if (AppendResult::NewRun == r) {
    bridge_.resetView();
    AppLog::info(kTagPlot, QStringLiteral("run #%1 started").arg(buffer_.runNumber()));
  }
  bridge_.markDirty();
}

void TelemetryView::applyGoal(const QJsonObject & body)
{
  proto::GoalInfo g;
  QString err;
  if (false == proto::parseGoal(body, g, err)) {
    AppLog::warn(kTagPlot, QStringLiteral("bad goal (ignored): %1").arg(err));
    return;
  }
  if (true == autoStartChk_->isChecked()) {
    buffer_.startRun(g.t);
    bridge_.resetView();
    bridge_.markDirty();
    AppLog::info(kTagPlot, QStringLiteral("run #%1 started by goal (%2): x=%3 y=%4 yaw=%5")
      .arg(buffer_.runNumber()).arg(g.frame)
      .arg(g.x, 0, 'f', 3).arg(g.y, 0, 'f', 3).arg(g.yaw, 0, 'f', 3));
  }
}

void TelemetryView::drainQueue()
{
  const std::size_t n = queue_.size();
  while (false == queue_.empty()) {
    const Queued q = queue_.front();
    queue_.pop_front();
    if (true == q.isGoal) {
      applyGoal(q.body);
    } else {
      applyTelemetry(q.body);
    }
  }
  if (0U < n) {
    AppLog::info(kTagPlot, QStringLiteral("resumed: applied %1 queued message(s)%2").arg(n)
      .arg((0 < droppedQueued_) ? QStringLiteral(", %1 oldest dropped").arg(droppedQueued_) :
      QString()));
  }
  droppedQueued_ = 0;
}

/* ===========================================================================
 * 조작
 * =========================================================================== */
void TelemetryView::onPauseToggled(bool on)
{
  pauseBtn_->setText((true == on) ? tr("실시간으로") : tr("일시정지"));
  if (true == on) {
    bridge_.setPaused(true);
  } else {
    bridge_.setPaused(false);
    drainQueue();   // 멈춘 동안 받은 알림 반영 → 다음 갱신에서 최신 구간으로
  }
  updateStatus();
}

void TelemetryView::onWindowChanged(int index)
{
  bridge_.setWindowSeconds(windowCombo_->itemData(index).toDouble());
}

void TelemetryView::clearPlots()
{
  queue_.clear();
  buffer_.clear();
  bridge_.resetView();
  bridge_.setPaused(false);
  bridge_.markDirty();
  AppLog::info(kTagPlot, QStringLiteral("cleared"));
  updateStatus();
}

void TelemetryView::setConnected(bool connected)
{
  connected_ = connected;
  updateStatus();
}

void TelemetryView::setUnavailable(const QString & reason)
{
  unavailable_ = reason;
  updateStatus();
}

void TelemetryView::updateStatus()
{
  QString text;
  if (false == unavailable_.isEmpty()) {
    text = unavailable_;
  } else if (true == buffer_.isEmpty()) {
    text = (true == connected_) ? tr("데이터 기다리는 중 (목표를 보내면 시작)") : tr("연결 안 됨");
  } else {
    text = tr("이동 #%1 · %2 s · %3 Hz")
      .arg(buffer_.runNumber())
      .arg(buffer_.lastTime(), 0, 'f', 1)
      .arg(buffer_.rateHz(), 0, 'f', 1);
    if (true == bridge_.paused()) {
      text += tr(" · 일시정지 (대기 %1)").arg(queue_.size());
    }
    if (false == connected_) {
      text += tr(" · 연결 끊김");
    }
  }
  statusLabel_->setText(text);
}

}  // namespace amr
