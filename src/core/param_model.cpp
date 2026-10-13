/**
 * @file    param_model.cpp
 * @brief   파라미터 표 모델 구현
 */
#include "core/param_model.hpp"

#include <QBrush>
#include <QColor>

#include <cmath>

namespace amr
{

using proto::ParamInfo;
using proto::ParamType;

namespace
{
/* 색 (밝은 배경에서 글자가 잘 보이는 연한 색) */
const QColor kPendingBg(0xFF, 0xF3, 0xBF);    // 노랑: 변경 예정
const QColor kChangedBg(0xD0, 0xEB, 0xFF);    // 파랑: 방금 바뀜
const QColor kReadOnlyFg(0x86, 0x8E, 0x96);   // 회색: 읽기 전용

/** 범위 끝값 비교 여유 (화면 입력 반올림 오차 흡수) */
constexpr double kRangeEps = 1.0e-9;

QString numberText(double d)
{
  return QString::number(d, 'g', 6);
}
}  // namespace

ParamModel::ParamModel(QObject * parent)
: QAbstractTableModel(parent)
{
}

int ParamModel::rowCount(const QModelIndex & parent) const
{
  return (true == parent.isValid()) ? 0 : static_cast<int>(params_.size());
}

int ParamModel::columnCount(const QModelIndex & parent) const
{
  return (true == parent.isValid()) ? 0 : static_cast<int>(ColumnCount);
}

QVariant ParamModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if ((Qt::Horizontal != orientation) || (Qt::DisplayRole != role)) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  QVariant out;
  switch (section) {
    case ColName:
      out = tr("이름");
      break;
    case ColCurrent:
      out = tr("현재 값");
      break;
    case ColPending:
      out = tr("변경 값");
      break;
    case ColRange:
      out = tr("범위");
      break;
    case ColDescription:
      out = tr("설명");
      break;
    default:
      break;
  }
  return out;
}

Qt::ItemFlags ParamModel::flags(const QModelIndex & index) const
{
  if (false == index.isValid()) {
    return Qt::NoItemFlags;
  }
  Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  const ParamInfo & p = params_.at(index.row());
  if ((ColPending == index.column()) && (false == p.readOnly) &&
    (ParamType::Unsupported != p.type))
  {
    f |= Qt::ItemIsEditable;
  }
  return f;
}

QVariant ParamModel::data(const QModelIndex & index, int role) const
{
  if ((false == index.isValid()) || (index.row() >= params_.size())) {
    return QVariant();
  }
  const ParamInfo & p = params_.at(index.row());
  const auto pit = pending_.constFind(p.name);
  const bool isPending = (pending_.cend() != pit);
  const int col = index.column();

  switch (role) {
    case Qt::DisplayRole:
      if (ColName == col) {
        return p.name;
      }
      if (ColCurrent == col) {
        return (ParamType::Unsupported == p.type) ? tr("(표시 안 함)") :
               proto::displayValue(p.value, p.type);
      }
      if (ColPending == col) {
        if (true == p.readOnly) {
          return tr("읽기 전용");
        }
        return (true == isPending) ? proto::displayValue(pit.value(), p.type) : QString();
      }
      if (ColRange == col) {
        return rangeText(p);
      }
      if (ColDescription == col) {
        return p.description;
      }
      break;

    case Qt::EditRole:
      /* 편집기를 열 때의 시작 값: 변경 예정이 있으면 그 값, 없으면 현재 값 */
      if (ColPending == col) {
        return (true == isPending) ? pit.value() : p.value;
      }
      break;

    case Qt::BackgroundRole:
      if ((true == isPending) && ((ColPending == col) || (ColName == col))) {
        return QBrush(kPendingBg);
      }
      if ((true == highlighted_.contains(p.name)) && (ColCurrent == col)) {
        return QBrush(kChangedBg);
      }
      break;

    case Qt::ForegroundRole:
      if (true == p.readOnly) {
        return QBrush(kReadOnlyFg);
      }
      break;

    case Qt::TextAlignmentRole:
      if ((ColCurrent == col) || (ColPending == col)) {
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
      }
      break;

    case Qt::ToolTipRole:
      return tooltip(p);

    case TypeRole:
      return static_cast<int>(p.type);
    case HasRangeRole:
      return p.hasRange;
    case MinRole:
      return p.min;
    case MaxRole:
      return p.max;
    case StepRole:
      return p.step;
    case NameRole:
      return p.name;
    case ReadOnlyRole:
      return p.readOnly;
    case PendingRole:
      return isPending;
    default:
      break;
  }
  return QVariant();
}

bool ParamModel::setData(const QModelIndex & index, const QVariant & value, int role)
{
  if ((false == index.isValid()) || (Qt::EditRole != role) || (ColPending != index.column())) {
    return false;
  }
  const QString name = params_.at(index.row()).name;
  QString err;
  if (false == setPending(name, value, err)) {
    emit editRejected(name, err);
    return false;
  }
  return true;
}

/* ===========================================================================
 * 목록
 * =========================================================================== */
void ParamModel::setParams(const QList<ParamInfo> & params)
{
  beginResetModel();
  params_ = params;
  rebuildIndex();
  highlighted_.clear();

  /* 새로 고침해도 사용자가 입력해 둔 변경 예정 값은 잃지 않는다.
   * 단, 이름이 사라졌거나 형식이 바뀌었거나 이제 현재 값과 같으면 지운다. */
  for (auto it = pending_.begin(); it != pending_.end(); ) {
    const ParamInfo * p = find(it.key());
    QString err;
    const bool keep = (nullptr != p) && (false == p->readOnly) &&
      (ParamType::Unsupported != p->type) &&
      (true == convert(it.value(), p->type, err).isValid()) &&
      (false == proto::sameValue(it.value(), p->value, p->type));
    if (true == keep) {
      ++it;
    } else {
      it = pending_.erase(it);
    }
  }
  endResetModel();
  emit pendingCountChanged(pendingCount());
}

void ParamModel::clear()
{
  beginResetModel();
  params_.clear();
  index_.clear();
  pending_.clear();
  highlighted_.clear();
  endResetModel();
  emit pendingCountChanged(0);
}

const ParamInfo * ParamModel::find(const QString & name) const
{
  const auto it = index_.constFind(name);
  return (index_.cend() == it) ? nullptr : &params_.at(it.value());
}

int ParamModel::rowOf(const QString & name) const
{
  return index_.value(name, -1);
}

/* ===========================================================================
 * 변경 예정
 * =========================================================================== */
QVariant ParamModel::convert(const QVariant & in, ParamType type, QString & err)
{
  QVariant out;
  bool ok = false;
  switch (type) {
    case ParamType::Double: {
        const double d = in.toString().trimmed().toDouble(&ok);
        if ((true == ok) && (true == std::isfinite(d))) {
          out = d;
        } else {
          err = tr("실수가 아닙니다: %1").arg(in.toString());
        }
        break;
      }
    case ParamType::Int: {
        const qlonglong n = in.toString().trimmed().toLongLong(&ok);
        if (true == ok) {
          out = static_cast<qint64>(n);
        } else {
          err = tr("정수가 아닙니다: %1").arg(in.toString());
        }
        break;
      }
    case ParamType::Bool:
      if (QMetaType::Bool == in.typeId()) {
        out = in.toBool();
      } else {
        const QString s = in.toString().trimmed().toLower();
        if ((QLatin1String("true") == s) || (QLatin1String("1") == s)) {
          out = true;
        } else if ((QLatin1String("false") == s) || (QLatin1String("0") == s)) {
          out = false;
        } else {
          err = tr("true 또는 false여야 합니다: %1").arg(in.toString());
        }
      }
      break;
    case ParamType::String:
      out = in.toString();
      break;
    case ParamType::Unsupported:
    default:
      err = tr("편집할 수 없는 형식입니다");
      break;
  }
  return out;
}

bool ParamModel::inRange(const ParamInfo & p, const QVariant & v, QString & err)
{
  if ((false == p.hasRange) ||
    ((ParamType::Double != p.type) && (ParamType::Int != p.type)))
  {
    return true;
  }
  const double d = v.toDouble();
  const double span = std::fmax(1.0, std::fabs(p.max - p.min));
  if ((d < (p.min - (kRangeEps * span))) || (d > (p.max + (kRangeEps * span)))) {
    err = tr("범위 밖입니다: %1 (허용 %2 ~ %3)")
          .arg(numberText(d), numberText(p.min), numberText(p.max));
    return false;
  }
  return true;
}

bool ParamModel::setPending(const QString & name, const QVariant & value, QString & err)
{
  const int row = rowOf(name);
  if (0 > row) {
    err = tr("없는 파라미터입니다");
    return false;
  }
  const ParamInfo & p = params_.at(row);
  if (true == p.readOnly) {
    err = tr("읽기 전용입니다 (시작할 때만 읽는 값)");
    return false;
  }
  const QVariant v = convert(value, p.type, err);
  if (false == v.isValid()) {
    return false;
  }
  if (false == inRange(p, v, err)) {
    return false;
  }

  if (true == proto::sameValue(v, p.value, p.type)) {
    (void)pending_.remove(name);   // 현재 값으로 되돌림 = 변경 없음
  } else {
    pending_.insert(name, v);
  }
  emitRowChanged(row);
  emit pendingCountChanged(pendingCount());
  return true;
}

void ParamModel::clearPending(const QString & name)
{
  if (0 < pending_.remove(name)) {
    emitRowChanged(rowOf(name));
    emit pendingCountChanged(pendingCount());
  }
}

void ParamModel::clearAllPending()
{
  if (true == pending_.isEmpty()) {
    return;
  }
  const QStringList names = pending_.keys();
  pending_.clear();
  for (const QString & n : names) {
    emitRowChanged(rowOf(n));
  }
  emit pendingCountChanged(0);
}

QJsonObject ParamModel::pendingJson() const
{
  QJsonObject o;
  for (auto it = pending_.cbegin(); it != pending_.cend(); ++it) {
    const ParamInfo * p = find(it.key());
    if (nullptr != p) {
      o.insert(it.key(), proto::toJson(it.value(), p->type));
    }
  }
  return o;
}

/* ===========================================================================
 * 다른 곳에서 바뀐 값
 * =========================================================================== */
QStringList ParamModel::applyRemote(const QJsonObject & params)
{
  QStringList changed;
  bool pendingTouched = false;
  for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
    const int row = rowOf(it.key());
    if (0 > row) {
      continue;   // 목록에 없는 이름 (새로 고침 전에 생긴 파라미터 등)
    }
    ParamInfo & p = params_[row];
    const QVariant v = proto::fromJson(it.value(), p.type);
    if (false == v.isValid()) {
      continue;
    }
    if (false == proto::sameValue(v, p.value, p.type)) {
      p.value = v;
      highlighted_.insert(p.name);
      changed.append(p.name);
    }
    /* 변경 예정 값과 새 현재 값이 같으면 적용이 끝난 것이므로 지운다 */
    const auto pit = pending_.find(p.name);
    if ((pending_.end() != pit) && (true == proto::sameValue(pit.value(), p.value, p.type))) {
      pending_.erase(pit);
      pendingTouched = true;
    }
    emitRowChanged(row);
  }
  if (true == pendingTouched) {
    emit pendingCountChanged(pendingCount());
  }
  return changed;
}

void ParamModel::clearHighlights()
{
  if (true == highlighted_.isEmpty()) {
    return;
  }
  const QSet<QString> names = highlighted_;
  highlighted_.clear();
  for (const QString & n : names) {
    emitRowChanged(rowOf(n));
  }
}

/* ===========================================================================
 * 조건 파일
 * =========================================================================== */
bool ParamModel::isInternal(const QString & name)
{
  return name.startsWith(QLatin1String("qos_overrides.")) ||
         (QLatin1String("use_sim_time") == name) ||
         (QLatin1String("start_type_description_service") == name);
}

QJsonObject ParamModel::savableValues() const
{
  QJsonObject o;
  for (const ParamInfo & p : params_) {
    if ((false == p.readOnly) && (ParamType::Unsupported != p.type) &&
      (false == isInternal(p.name)) && (true == p.value.isValid()))
    {
      o.insert(p.name, proto::toJson(p.value, p.type));
    }
  }
  return o;
}

ParamModel::LoadResult ParamModel::loadPending(const QJsonObject & params)
{
  LoadResult r;
  for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
    const ParamInfo * p = find(it.key());
    if (nullptr == p) {
      r.skipped.append(tr("%1: 없는 파라미터").arg(it.key()));
      continue;
    }
    /* 파일의 JSON 값을 문자열로 바꿔 화면 입력과 같은 검사를 거친다 */
    QVariant raw;
    if (true == it.value().isBool()) {
      raw = it.value().toBool();
    } else if (true == it.value().isDouble()) {
      raw = QString::number(it.value().toDouble(), 'g', 17);
    } else {
      raw = it.value().toVariant();
    }
    QString err;
    if (false == setPending(it.key(), raw, err)) {
      r.skipped.append(QStringLiteral("%1: %2").arg(it.key(), err));
    } else if (true == pending_.contains(it.key())) {
      ++r.staged;
    } else {
      ++r.unchanged;
    }
  }
  return r;
}

/* ===========================================================================
 * 내부
 * =========================================================================== */
void ParamModel::rebuildIndex()
{
  index_.clear();
  for (int i = 0; i < params_.size(); ++i) {
    index_.insert(params_.at(i).name, i);
  }
}

void ParamModel::emitRowChanged(int row)
{
  if ((0 <= row) && (row < params_.size())) {
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
  }
}

QString ParamModel::rangeText(const ParamInfo & p) const
{
  if (true == p.hasRange) {
    return QStringLiteral("%1 ~ %2").arg(numberText(p.min), numberText(p.max));
  }
  return QString();
}

QString ParamModel::tooltip(const ParamInfo & p) const
{
  QString t = QStringLiteral("<b>%1</b> (%2)").arg(p.name.toHtmlEscaped(), proto::typeToString(p.type));
  if (false == p.description.isEmpty()) {
    t += QStringLiteral("<br>") + p.description.toHtmlEscaped();
  }
  if (true == p.hasRange) {
    t += QStringLiteral("<br>") + tr("범위: %1").arg(rangeText(p));
  }
  if (true == p.readOnly) {
    t += QStringLiteral("<br>") + tr("읽기 전용: 노드 시작 시에만 읽는 값");
  }
  return t;
}

}  // namespace amr
