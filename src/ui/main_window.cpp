/**
 * @file    main_window.cpp
 * @brief   AMR Tuner 주 화면 구현
 */
#include "ui/main_window.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

#include "core/condition_file.hpp"
#include "ui/param_delegate.hpp"

namespace amr
{

namespace
{
const QString kTagNet = QStringLiteral("NET");
const QString kTagParam = QStringLiteral("PARAM");
const QString kTagFile = QStringLiteral("FILE");
const QString kTagUi = QStringLiteral("UI");
const QString kTagPlot = QStringLiteral("PLOT");

constexpr int kHighlightMs = 1500;     // 바뀐 값 파란색 표시 시간
constexpr int kLogMaxLines = 3000;     // 로그 창 최대 줄 수 (오래된 줄부터 지움)
constexpr quint16 kDefaultPort = 9310U;

/**
 * 막지 않는 알림 창.
 * 통신 응답 처리 중에 모달 창(exec)을 띄우면 그동안 소켓 수신 처리가 중첩되어
 * 순서가 꼬일 수 있으므로, open()으로 띄우고 바로 돌아온다.
 */
void showMessage(QWidget * parent, QMessageBox::Icon icon, const QString & title, const QString & text)
{
  auto * box = new QMessageBox(icon, title, text, QMessageBox::Ok, parent);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->open();
}
}  // namespace

MainWindow::MainWindow(QWidget * parent)
: QMainWindow(parent)
{
  buildUi();
  loadSettings();

  connect(&AppLog::instance(), &AppLog::logged, this, &MainWindow::onLog);
  connect(&client_, &GatewayClient::stateChanged, this, &MainWindow::onStateChanged);
  connect(&client_, &GatewayClient::eventReceived, this, &MainWindow::onEvent);
  connect(&model_, &ParamModel::pendingCountChanged, this, &MainWindow::onPendingCountChanged);
  connect(&model_, &ParamModel::editRejected, this,
    [this](const QString & name, const QString & reason) {
      AppLog::warn(kTagParam, QStringLiteral("%1: %2").arg(name, reason));
      statusBar()->showMessage(QStringLiteral("%1: %2").arg(name, reason), 5000);
    });
  connect(&model_, &QAbstractItemModel::modelReset, this, &MainWindow::updateCountLabel);
  connect(&proxy_, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateCountLabel);
  connect(&proxy_, &QAbstractItemModel::rowsRemoved, this, &MainWindow::updateCountLabel);
  connect(&proxy_, &QAbstractItemModel::modelReset, this, &MainWindow::updateCountLabel);
  connect(&proxy_, &QAbstractItemModel::layoutChanged, this, &MainWindow::updateCountLabel);

  highlightTimer_.setSingleShot(true);
  highlightTimer_.setInterval(kHighlightMs);
  connect(&highlightTimer_, &QTimer::timeout, &model_, &ParamModel::clearHighlights);

  updateButtons();
  updateStatus();
  updateCountLabel();
  AppLog::info(kTagUi, QStringLiteral("AMR Tuner started (Qt %1)").arg(QString::fromLatin1(qVersion())));
}

MainWindow::~MainWindow()
{
  /* 화면이 사라지는 중에 로그 신호가 오지 않도록 연결을 먼저 끊는다 */
  disconnect(&AppLog::instance(), nullptr, this, nullptr);
}

/* ===========================================================================
 * 화면 구성
 * =========================================================================== */
void MainWindow::buildUi()
{
  setWindowTitle(tr("AMR Tuner - PID 파라미터 조정"));
  resize(1280, 860);

  auto * central = new QWidget(this);
  auto * root = new QVBoxLayout(central);
  root->setContentsMargins(8, 8, 8, 4);
  root->setSpacing(6);

  /* ---- 1줄: 접속 ---- */
  auto * row1 = new QHBoxLayout();
  row1->addWidget(new QLabel(tr("주소"), central));
  hostEdit_ = new QLineEdit(central);
  hostEdit_->setPlaceholderText(QStringLiteral("127.0.0.1"));
  hostEdit_->setMaximumWidth(180);
  row1->addWidget(hostEdit_);
  row1->addWidget(new QLabel(tr("포트"), central));
  portSpin_ = new QSpinBox(central);
  portSpin_->setRange(1, 65535);
  portSpin_->setValue(kDefaultPort);
  row1->addWidget(portSpin_);
  connectBtn_ = new QPushButton(tr("접속"), central);
  connectBtn_->setMinimumWidth(80);
  row1->addWidget(connectBtn_);
  row1->addSpacing(12);
  connLabel_ = new QLabel(central);
  row1->addWidget(connLabel_);
  row1->addStretch(1);
  root->addLayout(row1);

  /* ---- 2줄: 거르기와 명령 ---- */
  auto * row2 = new QHBoxLayout();
  searchEdit_ = new QLineEdit(central);
  searchEdit_->setPlaceholderText(tr("이름·설명 검색 (예: linear, 이득)"));
  searchEdit_->setClearButtonEnabled(true);
  searchEdit_->setMinimumWidth(230);
  searchEdit_->setMaximumWidth(300);
  row2->addWidget(searchEdit_);
  hideInternalChk_ = new QCheckBox(tr("ROS 내부 항목 숨기기"), central);
  hideInternalChk_->setChecked(true);
  hideInternalChk_->setToolTip(tr("qos_overrides.*, use_sim_time 등 ROS가 자동으로 만드는 항목"));
  row2->addWidget(hideInternalChk_);
  pendingOnlyChk_ = new QCheckBox(tr("변경 예정만"), central);
  row2->addWidget(pendingOnlyChk_);
  refreshBtn_ = new QPushButton(tr("새로 고침"), central);
  refreshBtn_->setToolTip(tr("노드에서 목록과 현재 값을 다시 받습니다 (변경 예정 값은 유지)"));
  row2->addWidget(refreshBtn_);
  row2->addStretch(1);
  pendingLabel_ = new QLabel(central);
  row2->addWidget(pendingLabel_);
  applyBtn_ = new QPushButton(tr("적용"), central);
  applyBtn_->setToolTip(tr("변경 예정 값을 한 번에 노드로 보냅니다 (하나라도 거부되면 전부 취소)"));
  applyBtn_->setDefault(false);
  row2->addWidget(applyBtn_);
  revertBtn_ = new QPushButton(tr("되돌리기"), central);
  revertBtn_->setToolTip(tr("변경 예정 값을 모두 지웁니다 (노드 값은 그대로)"));
  row2->addWidget(revertBtn_);
  row2->addSpacing(12);
  saveBtn_ = new QPushButton(tr("조건 저장"), central);
  saveBtn_->setToolTip(tr("현재 값(적용된 값)을 시험 조건 파일로 저장합니다"));
  row2->addWidget(saveBtn_);
  loadBtn_ = new QPushButton(tr("조건 불러오기"), central);
  loadBtn_->setToolTip(tr("시험 조건 파일의 값을 변경 예정으로 불러옵니다 (적용은 직접)"));
  row2->addWidget(loadBtn_);
  root->addLayout(row2);

  /* ---- 표 ---- */
  proxy_.setSourceModel(&model_);
  table_ = new QTableView(central);
  table_->setModel(&proxy_);
  table_->setItemDelegateForColumn(ParamModel::ColPending, new ParamDelegate(table_));
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
    QAbstractItemView::AnyKeyPressed);
  table_->setAlternatingRowColors(true);
  table_->setWordWrap(false);
  table_->verticalHeader()->setVisible(false);
  table_->verticalHeader()->setDefaultSectionSize(24);
  QHeaderView * hh = table_->horizontalHeader();
  hh->setSectionResizeMode(ParamModel::ColName, QHeaderView::Interactive);
  hh->setSectionResizeMode(ParamModel::ColCurrent, QHeaderView::Interactive);
  hh->setSectionResizeMode(ParamModel::ColPending, QHeaderView::Interactive);
  hh->setSectionResizeMode(ParamModel::ColRange, QHeaderView::Interactive);
  hh->setSectionResizeMode(ParamModel::ColDescription, QHeaderView::Stretch);
  table_->setColumnWidth(ParamModel::ColName, 260);
  table_->setColumnWidth(ParamModel::ColCurrent, 100);
  table_->setColumnWidth(ParamModel::ColPending, 100);
  table_->setColumnWidth(ParamModel::ColRange, 110);
  table_->installEventFilter(this);

  /* ---- 로그 ---- */
  logView_ = new QPlainTextEdit(central);
  logView_->setReadOnly(true);
  logView_->setMaximumBlockCount(kLogMaxLines);
  logView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  logView_->setPlaceholderText(tr("로그"));

  /* ---- 탭: 파라미터 표 / 그래프 ---- */
  telemetryView_ = new TelemetryView(central);
  tabs_ = new QTabWidget(central);
  tabs_->addTab(table_, tr("파라미터"));
  tabs_->addTab(telemetryView_, tr("그래프"));

  splitter_ = new QSplitter(Qt::Vertical, central);
  splitter_->addWidget(tabs_);
  splitter_->addWidget(logView_);
  splitter_->setStretchFactor(0, 4);
  splitter_->setStretchFactor(1, 1);
  root->addWidget(splitter_, 1);
  setCentralWidget(central);

  /* ---- 상태 표시줄 ---- */
  nodeLabel_ = new QLabel(this);
  countLabel_ = new QLabel(this);
  statusBar()->addPermanentWidget(nodeLabel_);
  statusBar()->addPermanentWidget(countLabel_);

  /* ---- 신호 연결 ---- */
  connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
  connect(hostEdit_, &QLineEdit::returnPressed, this, &MainWindow::onConnectClicked);
  connect(refreshBtn_, &QPushButton::clicked, this, &MainWindow::requestHello);
  connect(applyBtn_, &QPushButton::clicked, this, &MainWindow::apply);
  connect(revertBtn_, &QPushButton::clicked, this, &MainWindow::revert);
  connect(saveBtn_, &QPushButton::clicked, this, &MainWindow::saveCondition);
  connect(loadBtn_, &QPushButton::clicked, this, &MainWindow::loadCondition);
  connect(searchEdit_, &QLineEdit::textChanged, &proxy_, &ParamFilterProxy::setSearchText);
  connect(hideInternalChk_, &QCheckBox::toggled, &proxy_, &ParamFilterProxy::setHideInternal);
  connect(pendingOnlyChk_, &QCheckBox::toggled, &proxy_, &ParamFilterProxy::setPendingOnly);
}

void MainWindow::loadSettings()
{
  QSettings s;
  hostEdit_->setText(s.value(QStringLiteral("net/host"), QStringLiteral("127.0.0.1")).toString());
  portSpin_->setValue(s.value(QStringLiteral("net/port"), kDefaultPort).toInt());
  const QByteArray geo = s.value(QStringLiteral("ui/geometry")).toByteArray();
  if (false == geo.isEmpty()) {
    (void)restoreGeometry(geo);
  }
  const QByteArray split = s.value(QStringLiteral("ui/splitter")).toByteArray();
  if (false == split.isEmpty()) {
    (void)splitter_->restoreState(split);
  }
  tabs_->setCurrentIndex(s.value(QStringLiteral("ui/tab"), 0).toInt());
  telemetryView_->loadSettings();
}

void MainWindow::saveSettings() const
{
  QSettings s;
  s.setValue(QStringLiteral("net/host"), hostEdit_->text().trimmed());
  s.setValue(QStringLiteral("net/port"), portSpin_->value());
  s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
  s.setValue(QStringLiteral("ui/splitter"), splitter_->saveState());
  s.setValue(QStringLiteral("ui/tab"), tabs_->currentIndex());
  telemetryView_->saveSettings();
}

/* ===========================================================================
 * 접속
 * =========================================================================== */
void MainWindow::onConnectClicked()
{
  if (GatewayClient::State::Disconnected == client_.state()) {
    QString host = hostEdit_->text().trimmed();
    if (true == host.isEmpty()) {
      host = QStringLiteral("127.0.0.1");
      hostEdit_->setText(host);
    }
    saveSettings();
    client_.connectTo(host, static_cast<quint16>(portSpin_->value()));
  } else {
    client_.disconnectFrom();
  }
}

void MainWindow::onStateChanged(GatewayClient::State state)
{
  if (GatewayClient::State::Connected == state) {
    requestHello();
  } else {
    nodeReady_ = false;
    busy_ = false;   // 대기 중이던 요청은 DISCONNECTED로 이미 끝남
  }
  telemetryView_->setConnected(GatewayClient::State::Connected == state);
  updateButtons();
  updateStatus();
}

void MainWindow::requestHello()
{
  (void)client_.send(QStringLiteral("hello"), QJsonObject(), [this](const Reply & r) {
      if (false == r.ok) {
        AppLog::error(kTagNet, QStringLiteral("hello failed: %1").arg(describe(r)));
        return;
      }
      nodeName_ = r.body.value(QStringLiteral("node")).toString();
      gatewayVersion_ = r.body.value(QStringLiteral("version")).toString();
      nodeReady_ = r.body.value(QStringLiteral("node_ready")).toBool();
      AppLog::info(kTagNet, QStringLiteral("gateway %1, node %2 (%3), gateway time %4").arg(
        gatewayVersion_, nodeName_, (true == nodeReady_) ? QStringLiteral("ready") :
        QStringLiteral("NOT running"), r.body.value(QStringLiteral("time")).toString()));
      updateStatus();
      if (true == nodeReady_) {
        refresh();
      } else {
        AppLog::warn(kTagNet, QStringLiteral("target node is not running: start it, then press [새로 고침]"));
      }
      requestSubscribe(proto::supportedEvents(r.body));
    });
}

void MainWindow::requestSubscribe(const QStringList & supported)
{
  /* 0.1.0 중계 노드는 telemetry·goal을 모르므로 함께 구독하면 요청 전체가 거부된다
   * → hello가 알려 준 목록에 있는 것만 구독한다 */
  QJsonArray events;
  events.append(QString::fromLatin1(proto::kEvtParamChanged));
  const bool graphs = supported.contains(QLatin1String(proto::kEvtTelemetry));
  if (true == graphs) {
    events.append(QString::fromLatin1(proto::kEvtTelemetry));
  }
  if (true == supported.contains(QLatin1String(proto::kEvtGoal))) {
    events.append(QString::fromLatin1(proto::kEvtGoal));
  }
  if (true == graphs) {
    telemetryView_->setUnavailable(QString());
  } else {
    const QString why = tr("중계 노드 %1는 그래프 알림을 지원하지 않음 (0.2.0 이상 필요)").arg(gatewayVersion_);
    telemetryView_->setUnavailable(why);
    AppLog::warn(kTagPlot, QStringLiteral("gateway %1 has no telemetry: update amr_param_gateway to 0.2.0+")
      .arg(gatewayVersion_));
  }

  QJsonObject body;
  body.insert(QStringLiteral("events"), events);
  (void)client_.send(QStringLiteral("subscribe"), body, [](const Reply & r) {
      if (false == r.ok) {
        AppLog::warn(kTagNet, QStringLiteral("subscribe failed: %1").arg(describe(r)));
      } else {
        QStringList names;
        for (const QJsonValue & v : r.body.value(QStringLiteral("subscribed")).toArray()) {
          names.append(v.toString());
        }
        AppLog::info(kTagNet, QStringLiteral("subscribed: %1").arg(names.join(QStringLiteral(", "))));
      }
    });
}

void MainWindow::refresh()
{
  if (true == busy_) {
    return;
  }
  busy_ = true;
  updateButtons();
  (void)client_.send(QStringLiteral("list"), QJsonObject(), [this](const Reply & r) {
      busy_ = false;
      if (true == r.ok) {
        QList<proto::ParamInfo> params;
        QString err;
        if (true == proto::parseParamList(r.body, params, err)) {
          model_.setParams(params);
          int ro = 0;
          for (const auto & p : params) {
            ro += (true == p.readOnly) ? 1 : 0;
          }
          AppLog::info(kTagParam, QStringLiteral("loaded %1 parameters (%2 read-only)")
            .arg(params.size()).arg(ro));
        } else {
          AppLog::error(kTagParam, QStringLiteral("bad list response: %1").arg(err));
        }
      } else {
        if (QLatin1String("NODE_NOT_FOUND") == r.code) {
          nodeReady_ = false;
        }
        AppLog::error(kTagParam, QStringLiteral("list failed: %1").arg(describe(r)));
      }
      updateButtons();
      updateStatus();
    });
}

void MainWindow::onEvent(const QString & evt, const QJsonObject & body)
{
  if (QLatin1String(proto::kEvtTelemetry) == evt) {
    /* 초당 20줄: 로그를 남기지 않고 그래프로만 보낸다 (가장 잦은 알림이라 먼저 검사) */
    telemetryView_->onTelemetry(body);
  } else if (QLatin1String(proto::kEvtGoal) == evt) {
    telemetryView_->onGoal(body);
  } else if (QLatin1String(proto::kEvtParamChanged) == evt) {
    const QJsonObject params = body.value(QStringLiteral("params")).toObject();
    const QStringList changed = model_.applyRemote(params);
    if (false == changed.isEmpty()) {
      QStringList parts;
      for (const QString & n : changed) {
        const proto::ParamInfo * p = model_.find(n);
        if (nullptr != p) {
          parts.append(QStringLiteral("%1=%2").arg(n, proto::displayValue(p->value, p->type)));
        }
      }
      AppLog::info(kTagParam, QStringLiteral("changed on node (%1): %2")
        .arg(body.value(QStringLiteral("stamp")).toString(), parts.join(QStringLiteral(", "))));
      highlightTimer_.start();
    }
    /* 노드가 새로 시작되면 new_parameters로 전체가 다시 온다 → 노드가 살아났다고 본다 */
    if (false == nodeReady_) {
      nodeReady_ = true;
      updateStatus();
      updateButtons();
      if (0 == model_.rowCount()) {
        refresh();
      }
    }
  } else if (QLatin1String("server_full") == evt) {
    AppLog::error(kTagNet, QStringLiteral("gateway refused connection: too many clients"));
    showMessage(this, QMessageBox::Warning, tr("접속 거부"),
      tr("중계 노드의 동시 접속 수 한도를 넘었습니다.\n다른 앱 창이나 gw_cli watch를 닫고 다시 접속하세요."));
  } else {
    AppLog::info(kTagNet, QStringLiteral("event: %1").arg(evt));
  }
}

/* ===========================================================================
 * 적용·되돌리기
 * =========================================================================== */
void MainWindow::apply()
{
  if ((true == busy_) || (false == client_.isConnected()) || (0 == model_.pendingCount())) {
    return;
  }
  const QJsonObject params = model_.pendingJson();
  QStringList changes;
  for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
    const proto::ParamInfo * p = model_.find(it.key());
    if (nullptr != p) {
      changes.append(QStringLiteral("%1 %2 -> %3").arg(it.key(),
        proto::displayValue(p->value, p->type),
        proto::displayValue(proto::fromJson(it.value(), p->type), p->type)));
    }
  }
  AppLog::info(kTagParam, QStringLiteral("apply %1 change(s): %2")
    .arg(params.size()).arg(changes.join(QStringLiteral(", "))));

  busy_ = true;
  updateButtons();
  QJsonObject body;
  body.insert(QStringLiteral("params"), params);
  (void)client_.send(QStringLiteral("set"), body, [this](const Reply & r) {
      busy_ = false;
      if (true == r.ok) {
        (void)model_.applyRemote(r.body.value(QStringLiteral("applied")).toObject());
        highlightTimer_.start();
        AppLog::info(kTagParam, QStringLiteral("applied"));
        statusBar()->showMessage(tr("적용 완료"), 3000);
      } else {
        const QString why = describe(r);
        AppLog::error(kTagParam, QStringLiteral("apply rejected: %1").arg(why));
        showMessage(this, QMessageBox::Warning, tr("적용 거부"),
          tr("%1\n\n아무 값도 바뀌지 않았습니다. (전부 적용 또는 전부 거부)\n"
             "변경 예정 값을 고친 뒤 다시 적용하세요.").arg(why));
      }
      updateButtons();
    });
}

void MainWindow::revert()
{
  const int n = model_.pendingCount();
  if (0 < n) {
    model_.clearAllPending();
    AppLog::info(kTagParam, QStringLiteral("reverted %1 pending change(s)").arg(n));
  }
}

/* ===========================================================================
 * 조건 파일
 * =========================================================================== */
void MainWindow::saveCondition()
{
  if (0 == model_.rowCount()) {
    return;
  }
  if (0 < model_.pendingCount()) {
    const auto ans = QMessageBox::question(this, tr("조건 저장"),
      tr("적용하지 않은 변경 예정 %1개는 저장되지 않습니다.\n현재 값(노드에 적용된 값)으로 저장할까요?")
      .arg(model_.pendingCount()));
    if (QMessageBox::Yes != ans) {
      return;
    }
  }
  bool ok = false;
  const QString note = QInputDialog::getMultiLineText(this, tr("조건 저장"),
    tr("메모 (예: kp05_kd20, 출발 거리 2 m, rosbag 이름)"), QString(), &ok);
  if (false == ok) {
    return;
  }
  QSettings s;
  const QString dir = s.value(QStringLiteral("file/lastDir"), QDir::homePath()).toString();
  const QString path = QFileDialog::getSaveFileName(this, tr("조건 저장"),
    QDir(dir).filePath(ConditionFile::defaultFileName()), tr("조건 파일 (*.json)"));
  if (true == path.isEmpty()) {
    return;
  }

  Condition c;
  c.node = nodeName_;
  c.note = note;
  c.params = model_.savableValues();
  QString err;
  if (false == ConditionFile::save(path, c, err)) {
    AppLog::error(kTagFile, QStringLiteral("save failed: %1 (%2)").arg(path, err));
    showMessage(this, QMessageBox::Critical, tr("저장 실패"), err);
    return;
  }
  s.setValue(QStringLiteral("file/lastDir"), QFileInfo(path).absolutePath());
  AppLog::info(kTagFile, QStringLiteral("saved %1 values to %2").arg(c.params.size()).arg(path));
  statusBar()->showMessage(tr("저장: %1").arg(QFileInfo(path).fileName()), 5000);
}

void MainWindow::loadCondition()
{
  if (0 == model_.rowCount()) {
    showMessage(this, QMessageBox::Information, tr("조건 불러오기"),
      tr("먼저 접속해 파라미터 목록을 받아야 합니다."));
    return;
  }
  QSettings s;
  const QString dir = s.value(QStringLiteral("file/lastDir"), QDir::homePath()).toString();
  const QString path = QFileDialog::getOpenFileName(this, tr("조건 불러오기"), dir,
    tr("조건 파일 (*.json);;모든 파일 (*)"));
  if (true == path.isEmpty()) {
    return;
  }
  Condition c;
  QString err;
  if (false == ConditionFile::load(path, c, err)) {
    AppLog::error(kTagFile, QStringLiteral("load failed: %1 (%2)").arg(path, err));
    showMessage(this, QMessageBox::Critical, tr("불러오기 실패"), err);
    return;
  }
  s.setValue(QStringLiteral("file/lastDir"), QFileInfo(path).absolutePath());
  if ((false == c.node.isEmpty()) && (c.node != nodeName_)) {
    AppLog::warn(kTagFile, QStringLiteral("file node %1 differs from connected node %2").arg(c.node, nodeName_));
  }

  /* 파일 하나가 시험 조건 전체이므로, 이전 변경 예정은 지우고 파일 값만 남긴다 */
  model_.clearAllPending();
  const ParamModel::LoadResult r = model_.loadPending(c.params);
  AppLog::info(kTagFile, QStringLiteral("loaded %1: %2 staged, %3 unchanged, %4 skipped")
    .arg(QFileInfo(path).fileName()).arg(r.staged).arg(r.unchanged).arg(r.skipped.size()));
  for (const QString & sk : r.skipped) {
    AppLog::warn(kTagFile, QStringLiteral("skipped %1").arg(sk));
  }

  QString text = tr("파일: %1\n저장 시각: %2\n메모: %3\n\n변경 예정 %4개, 현재와 같음 %5개, 건너뜀 %6개")
                 .arg(QFileInfo(path).fileName(), c.savedAt,
                      (true == c.note.isEmpty()) ? tr("(없음)") : c.note)
                 .arg(r.staged).arg(r.unchanged).arg(r.skipped.size());
  if (false == r.skipped.isEmpty()) {
    text += QStringLiteral("\n - ") + r.skipped.join(QStringLiteral("\n - "));
  }
  if (0 < r.staged) {
    text += tr("\n\n[적용]을 눌러야 노드에 반영됩니다.");
  }
  showMessage(this, QMessageBox::Information, tr("조건 불러오기"), text);
}

/* ===========================================================================
 * 표시 갱신
 * =========================================================================== */
void MainWindow::onPendingCountChanged(int count)
{
  Q_UNUSED(count);
  updateButtons();
}

void MainWindow::updateButtons()
{
  const auto st = client_.state();
  const bool connected = (GatewayClient::State::Connected == st);
  const bool hasRows = (0 < model_.rowCount());
  const int pending = model_.pendingCount();

  hostEdit_->setEnabled(GatewayClient::State::Disconnected == st);
  portSpin_->setEnabled(GatewayClient::State::Disconnected == st);
  connectBtn_->setText((GatewayClient::State::Disconnected == st) ? tr("접속") :
                       (GatewayClient::State::Connecting == st) ? tr("취소") : tr("끊기"));
  refreshBtn_->setEnabled(connected && (false == busy_));
  applyBtn_->setEnabled(connected && nodeReady_ && (false == busy_) && (0 < pending));
  revertBtn_->setEnabled(0 < pending);
  saveBtn_->setEnabled(hasRows);
  loadBtn_->setEnabled(hasRows);

  /* 변경 예정이 있으면 [적용]을 굵게 표시해 눈에 띄게 한다 */
  QFont af = applyBtn_->font();
  af.setBold(applyBtn_->isEnabled());
  applyBtn_->setFont(af);

  if (0 < pending) {
    pendingLabel_->setText(tr("<b>변경 예정 %1개</b>").arg(pending));
    pendingLabel_->setStyleSheet(QStringLiteral("QLabel { background:#FFF3BF; padding:2px 6px; }"));
  } else {
    pendingLabel_->setText(tr("변경 예정 없음"));
    pendingLabel_->setStyleSheet(QString());
  }
}

void MainWindow::updateStatus()
{
  QString dot;
  QString text;
  switch (client_.state()) {
    case GatewayClient::State::Connected:
      dot = QStringLiteral("#2B8A3E");
      text = tr("연결됨 %1:%2").arg(client_.host()).arg(client_.port());
      if (false == nodeReady_) {
        dot = QStringLiteral("#E67700");
        text += tr(" · 대상 노드 없음");
      }
      break;
    case GatewayClient::State::Connecting:
      dot = QStringLiteral("#E67700");
      text = tr("접속 중... %1:%2").arg(client_.host()).arg(client_.port());
      break;
    case GatewayClient::State::Disconnected:
    default:
      dot = QStringLiteral("#C92A2A");
      text = (0 < model_.rowCount()) ? tr("끊김 (표의 값은 마지막으로 받은 값)") : tr("끊김");
      break;
  }
  connLabel_->setText(QStringLiteral("<span style='color:%1'>&#9679;</span> %2").arg(dot, text.toHtmlEscaped()));

  if (false == nodeName_.isEmpty()) {
    nodeLabel_->setText(tr("노드 %1 · 중계 %2").arg(nodeName_, gatewayVersion_));
  } else {
    nodeLabel_->clear();
  }
}

void MainWindow::updateCountLabel()
{
  countLabel_->setText(tr("표시 %1 / 전체 %2").arg(proxy_.rowCount()).arg(model_.rowCount()));
}

void MainWindow::onLog(const QString & line, AppLog::Level level)
{
  QString color;
  if (AppLog::Level::Error == level) {
    color = QStringLiteral("#C92A2A");
  } else if (AppLog::Level::Warn == level) {
    color = QStringLiteral("#B35C00");
  } else {
    /* 기본 글자색 */
  }
  if (true == color.isEmpty()) {
    logView_->appendPlainText(line);
  } else {
    logView_->appendHtml(QStringLiteral("<span style='color:%1'>%2</span>").arg(color, line.toHtmlEscaped()));
  }
}

QString MainWindow::describe(const Reply & r)
{
  return (true == r.message.isEmpty()) ? r.code : QStringLiteral("%1 - %2").arg(r.code, r.message);
}

/* ===========================================================================
 * 창 이벤트
 * =========================================================================== */
bool MainWindow::eventFilter(QObject * watched, QEvent * event)
{
  /* 표에서 Delete 키: 선택한 줄의 변경 예정 값 취소 */
  if ((watched == table_) && (QEvent::KeyPress == event->type())) {
    const auto * ke = static_cast<QKeyEvent *>(event);
    if ((Qt::Key_Delete == ke->key()) || (Qt::Key_Backspace == ke->key())) {
      const QModelIndexList rows = table_->selectionModel()->selectedRows(ParamModel::ColName);
      for (const QModelIndex & pi : rows) {
        model_.clearPending(pi.data(ParamModel::NameRole).toString());
      }
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent * event)
{
  if (0 < model_.pendingCount()) {
    const auto ans = QMessageBox::question(this, tr("종료"),
      tr("적용하지 않은 변경 예정 %1개가 있습니다. 버리고 종료할까요?").arg(model_.pendingCount()));
    if (QMessageBox::Yes != ans) {
      event->ignore();
      return;
    }
  }
  saveSettings();
  client_.disconnectFrom();
  event->accept();
}

}  // namespace amr
