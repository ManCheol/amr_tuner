/**
 * @file    param_delegate.cpp
 * @brief   "변경 값" 칸 편집기 구현
 */
#include "ui/param_delegate.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLocale>
#include <QSpinBox>

#include <climits>
#include <cmath>

#include "core/gateway_protocol.hpp"
#include "core/param_model.hpp"

namespace amr
{

using proto::ParamType;

namespace
{
constexpr int kDecimals = 4;                 // 0.0001 단위까지 입력 (이득·거리에 충분)
constexpr double kUnboundedLimit = 1.0e9;    // 범위 정보가 없을 때의 입력 한계
}  // namespace

ParamDelegate::ParamDelegate(QObject * parent)
: QStyledItemDelegate(parent)
{
}

double ParamDelegate::niceStep(double span)
{
  if (0.0 >= span) {
    return 0.01;
  }
  const double raw = span / 100.0;
  const double mag = std::pow(10.0, std::floor(std::log10(raw)));
  const double norm = raw / mag;   // 1 이상 10 미만
  double nice = 1.0;
  if (norm >= 5.0) {
    nice = 5.0;
  } else if (norm >= 2.0) {
    nice = 2.0;
  } else {
    nice = 1.0;
  }
  return std::fmax(nice * mag, std::pow(10.0, -kDecimals));
}

QWidget * ParamDelegate::createEditor(
  QWidget * parent, const QStyleOptionViewItem & option, const QModelIndex & index) const
{
  const auto type = static_cast<ParamType>(index.data(ParamModel::TypeRole).toInt());
  const bool hasRange = index.data(ParamModel::HasRangeRole).toBool();
  const double lo = index.data(ParamModel::MinRole).toDouble();
  const double hi = index.data(ParamModel::MaxRole).toDouble();
  const double step = index.data(ParamModel::StepRole).toDouble();

  QWidget * editor = nullptr;
  switch (type) {
    case ParamType::Double: {
        auto * sb = new QDoubleSpinBox(parent);
        sb->setLocale(QLocale::c());   // 소수점은 항상 '.' (Windows 지역 설정과 무관)
        sb->setDecimals(kDecimals);
        if (true == hasRange) {
          sb->setRange(lo, hi);
          sb->setSingleStep((0.0 < step) ? step : niceStep(hi - lo));
        } else {
          sb->setRange(-kUnboundedLimit, kUnboundedLimit);
          sb->setSingleStep(0.01);
        }
        sb->setAccelerated(true);
        sb->setFrame(false);
        editor = sb;
        break;
      }
    case ParamType::Int: {
        auto * sb = new QSpinBox(parent);
        if (true == hasRange) {
          sb->setRange(static_cast<int>(std::fmax(lo, static_cast<double>(INT_MIN))),
            static_cast<int>(std::fmin(hi, static_cast<double>(INT_MAX))));
          sb->setSingleStep((1.0 <= step) ? static_cast<int>(step) : 1);
        } else {
          sb->setRange(INT_MIN, INT_MAX);
        }
        sb->setFrame(false);
        editor = sb;
        break;
      }
    case ParamType::Bool: {
        auto * cb = new QComboBox(parent);
        cb->addItem(QStringLiteral("true"), true);
        cb->addItem(QStringLiteral("false"), false);
        editor = cb;
        break;
      }
    case ParamType::String:
      editor = new QLineEdit(parent);
      break;
    case ParamType::Unsupported:
    default:
      editor = QStyledItemDelegate::createEditor(parent, option, index);
      break;
  }
  return editor;
}

void ParamDelegate::setEditorData(QWidget * editor, const QModelIndex & index) const
{
  const QVariant v = index.data(Qt::EditRole);
  if (auto * dsb = qobject_cast<QDoubleSpinBox *>(editor)) {
    /* 편집 전 값을 기억해 둔다: 소수 4자리로 반올림되어 보이므로, 손대지 않고 닫으면
     * 반올림 값이 아닌 원래 값을 돌려주기 위함 (의도치 않은 "변경 예정" 방지) */
    dsb->setProperty("origValue", v.toDouble());
    dsb->setValue(v.toDouble());
    dsb->selectAll();
  } else if (auto * sb = qobject_cast<QSpinBox *>(editor)) {
    sb->setValue(static_cast<int>(v.toLongLong()));
    sb->selectAll();
  } else if (auto * cb = qobject_cast<QComboBox *>(editor)) {
    cb->setCurrentIndex((true == v.toBool()) ? 0 : 1);
  } else if (auto * le = qobject_cast<QLineEdit *>(editor)) {
    le->setText(v.toString());
    le->selectAll();
  } else {
    QStyledItemDelegate::setEditorData(editor, index);
  }
}

void ParamDelegate::setModelData(
  QWidget * editor, QAbstractItemModel * model, const QModelIndex & index) const
{
  /* 실제 검사와 저장은 ParamModel::setData → setPending이 한다 */
  if (auto * dsb = qobject_cast<QDoubleSpinBox *>(editor)) {
    dsb->interpretText();
    double value = dsb->value();
    const QVariant orig = dsb->property("origValue");
    if ((true == orig.isValid()) && (dsb->valueFromText(dsb->textFromValue(orig.toDouble())) == value)) {
      value = orig.toDouble();   // 보이는 값이 그대로면 원래 값 유지
    }
    (void)model->setData(index, value, Qt::EditRole);
  } else if (auto * sb = qobject_cast<QSpinBox *>(editor)) {
    sb->interpretText();
    (void)model->setData(index, static_cast<qint64>(sb->value()), Qt::EditRole);
  } else if (auto * cb = qobject_cast<QComboBox *>(editor)) {
    (void)model->setData(index, cb->currentData().toBool(), Qt::EditRole);
  } else if (auto * le = qobject_cast<QLineEdit *>(editor)) {
    (void)model->setData(index, le->text(), Qt::EditRole);
  } else {
    QStyledItemDelegate::setModelData(editor, model, index);
  }
}

}  // namespace amr
