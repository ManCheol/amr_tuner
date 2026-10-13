/**
 * @file    param_filter_proxy.cpp
 * @brief   파라미터 표 걸러 보기 구현
 */
#include "ui/param_filter_proxy.hpp"

#include "core/param_model.hpp"

namespace amr
{

ParamFilterProxy::ParamFilterProxy(QObject * parent)
: QSortFilterProxyModel(parent)
{
  /* 변경 예정 표시가 바뀌면 "변경 예정만" 거르기 결과도 다시 계산해야 한다 */
  setDynamicSortFilter(true);
}

void ParamFilterProxy::setSearchText(const QString & text)
{
  beginFilterChange();
  search_ = text.trimmed();
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ParamFilterProxy::setHideInternal(bool hide)
{
  beginFilterChange();
  hideInternal_ = hide;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ParamFilterProxy::setPendingOnly(bool only)
{
  beginFilterChange();
  pendingOnly_ = only;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool ParamFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex & sourceParent) const
{
  const QModelIndex idx = sourceModel()->index(sourceRow, ParamModel::ColName, sourceParent);
  const QString name = idx.data(ParamModel::NameRole).toString();

  if ((true == hideInternal_) && (true == ParamModel::isInternal(name))) {
    return false;
  }
  if ((true == pendingOnly_) && (false == idx.data(ParamModel::PendingRole).toBool())) {
    return false;
  }
  if (false == search_.isEmpty()) {
    const QString desc =
      sourceModel()->index(sourceRow, ParamModel::ColDescription, sourceParent).data().toString();
    if ((false == name.contains(search_, Qt::CaseInsensitive)) &&
      (false == desc.contains(search_, Qt::CaseInsensitive)))
    {
      return false;
    }
  }
  return true;
}

}  // namespace amr
