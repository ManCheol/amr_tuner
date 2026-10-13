/**
 * @file    param_filter_proxy.hpp
 * @brief   파라미터 표 걸러 보기 (검색어, ROS 내부 항목 숨기기, 변경 예정만)
 */
#ifndef AMR_TUNER_UI_PARAM_FILTER_PROXY_HPP
#define AMR_TUNER_UI_PARAM_FILTER_PROXY_HPP

#include <QSortFilterProxyModel>
#include <QString>

namespace amr
{

class ParamFilterProxy : public QSortFilterProxyModel
{
  Q_OBJECT

public:
  explicit ParamFilterProxy(QObject * parent = nullptr);

  /** 이름·설명에 포함된 글자로 거르기 (대소문자 무시, 빈 문자열 = 전체) */
  void setSearchText(const QString & text);
  /** qos_overrides.*, use_sim_time 등 ROS가 자동으로 만드는 항목 숨기기 */
  void setHideInternal(bool hide);
  /** 변경 예정 항목만 보기 */
  void setPendingOnly(bool only);

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex & sourceParent) const override;

private:
  QString search_;
  bool hideInternal_{true};
  bool pendingOnly_{false};
};

}  // namespace amr

#endif  // AMR_TUNER_UI_PARAM_FILTER_PROXY_HPP
