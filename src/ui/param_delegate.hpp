/**
 * @file    param_delegate.hpp
 * @brief   "변경 값" 칸의 편집기 (형식·범위에 맞는 입력 상자)
 *
 *   실수 → QDoubleSpinBox (범위가 있으면 그 범위로 제한, 화살표 간격은 범위에 맞춰 자동)
 *   정수 → QSpinBox
 *   참/거짓 → QComboBox (true / false)
 *   문자열 → QLineEdit
 * 범위 제한은 입력 편의일 뿐이고, 최종 검사는 ParamModel과 노드가 한다.
 */
#ifndef AMR_TUNER_UI_PARAM_DELEGATE_HPP
#define AMR_TUNER_UI_PARAM_DELEGATE_HPP

#include <QStyledItemDelegate>

namespace amr
{

class ParamDelegate : public QStyledItemDelegate
{
  Q_OBJECT

public:
  explicit ParamDelegate(QObject * parent = nullptr);

  QWidget * createEditor(
    QWidget * parent, const QStyleOptionViewItem & option,
    const QModelIndex & index) const override;
  void setEditorData(QWidget * editor, const QModelIndex & index) const override;
  void setModelData(
    QWidget * editor, QAbstractItemModel * model, const QModelIndex & index) const override;

  /** 범위 폭에 맞는 화살표 간격 (폭의 약 1/100을 1·2·5 단위로 반올림) */
  static double niceStep(double span);
};

}  // namespace amr

#endif  // AMR_TUNER_UI_PARAM_DELEGATE_HPP
