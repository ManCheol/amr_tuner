/**
 * @file    param_model.hpp
 * @brief   파라미터 표 모델 (현재 값 + 변경 예정 값)
 *
 * ---------------------------------------------------------------------------
 * [두 단계 변경]
 *   값을 고치면 곧바로 로봇에 보내지 않고 "변경 예정"으로 표시만 한다(노란색).
 *   [적용]을 누르면 변경 예정 값을 한 번에 보내고, 노드는 전부 받아들이거나 전부
 *   거부한다(atomic). 이렇게 하는 이유:
 *     - kp와 kd처럼 함께 바꿔야 의미 있는 값을 한 번에 반영 (중간 조합으로 주행하지 않음)
 *     - 타자 실수가 바로 로봇 동작으로 이어지지 않음
 *     - 실험 조건을 불러온 뒤 확인하고 적용할 수 있음
 *
 * [열]
 *   이름 | 현재 값 | 변경 값 | 범위 | 설명
 *
 * [색]
 *   노란색 : 변경 예정 (아직 적용 안 됨)
 *   파란색 : 방금 바뀐 값 (적용 결과 또는 rqt 등 다른 곳에서 바꾼 값)
 *   회색 글자 : 읽기 전용 (시작할 때만 읽는 값)
 *
 * 값 검사(형식·범위·읽기 전용)를 모델에서 하므로 화면 편집기, 조건 파일 불러오기,
 * 시험 코드가 모두 같은 규칙을 따른다.
 * ---------------------------------------------------------------------------
 */
#ifndef AMR_TUNER_CORE_PARAM_MODEL_HPP
#define AMR_TUNER_CORE_PARAM_MODEL_HPP

#include <QAbstractTableModel>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "core/gateway_protocol.hpp"

namespace amr
{

class ParamModel : public QAbstractTableModel
{
  Q_OBJECT

public:
  enum Column : int
  {
    ColName = 0,
    ColCurrent = 1,
    ColPending = 2,
    ColRange = 3,
    ColDescription = 4,
    ColumnCount = 5
  };

  /** 편집기(대리자)가 형식·범위를 알기 위한 추가 역할 */
  enum Role : int
  {
    TypeRole = Qt::UserRole + 1,   ///< proto::ParamType (int)
    HasRangeRole,
    MinRole,
    MaxRole,
    StepRole,
    NameRole,
    ReadOnlyRole,
    PendingRole                    ///< 변경 예정이면 true
  };

  /** 조건 파일을 불러온 결과 */
  struct LoadResult
  {
    int staged{0};          ///< 변경 예정으로 넣은 수
    int unchanged{0};       ///< 현재 값과 같아 넣지 않은 수
    QStringList skipped;    ///< 넣지 못한 항목 ("이름: 사유")
  };

  explicit ParamModel(QObject * parent = nullptr);

  /* ---- QAbstractTableModel ---- */
  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  int columnCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
  Qt::ItemFlags flags(const QModelIndex & index) const override;
  bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole) override;

  /* ---- 목록 ---- */

  /** list 응답으로 표 전체를 바꾼다. 같은 이름·형식의 변경 예정 값은 유지한다. */
  void setParams(const QList<proto::ParamInfo> & params);
  void clear();
  const proto::ParamInfo * find(const QString & name) const;
  int rowOf(const QString & name) const;

  /* ---- 변경 예정 ---- */

  /**
   * 변경 예정 값 지정. 문자열("0.8", "true")도 형식에 맞게 바꿔 받는다.
   * 현재 값과 같으면 변경 예정에서 뺀다.
   * @return 실패하면 false와 사유 (읽기 전용, 형식 불일치, 범위 밖, 없는 이름)
   */
  bool setPending(const QString & name, const QVariant & value, QString & err);
  void clearPending(const QString & name);
  void clearAllPending();
  int pendingCount() const {return static_cast<int>(pending_.size());}
  /** set 요청의 "params"로 보낼 객체 */
  QJsonObject pendingJson() const;

  /* ---- 다른 곳에서 바뀐 값 반영 ---- */

  /**
   * set 응답의 applied 또는 param_changed 알림의 params를 현재 값에 반영한다.
   * 새 현재 값과 같은 변경 예정 값은 지운다(적용 완료).
   * @return 값이 실제로 바뀐 이름 목록 (로그용)
   */
  QStringList applyRemote(const QJsonObject & params);
  /** 파란색 표시 지우기 (화면에서 잠시 뒤 호출) */
  void clearHighlights();

  /* ---- 조건 파일 ---- */

  /** 저장할 값: 바꿀 수 있는 값만 (읽기 전용, ROS 내부 항목, 미지원 형식 제외) */
  QJsonObject savableValues() const;
  /** 조건 파일의 값을 변경 예정으로 넣는다 (바로 적용하지 않음) */
  LoadResult loadPending(const QJsonObject & params);

  /** ROS가 자동으로 만드는 항목인지 (qos_overrides.*, use_sim_time 등) */
  static bool isInternal(const QString & name);

  /** 문자열 등 입력값을 형식에 맞는 값으로 변환. 실패하면 무효 QVariant와 사유 */
  static QVariant convert(const QVariant & in, proto::ParamType type, QString & err);

signals:
  void pendingCountChanged(int count);
  /** 표에서 직접 고친 값이 거부됨 (화면에 사유 표시용) */
  void editRejected(const QString & name, const QString & reason);

private:
  void rebuildIndex();
  void emitRowChanged(int row);
  QString rangeText(const proto::ParamInfo & p) const;
  QString tooltip(const proto::ParamInfo & p) const;
  /** 범위 검사 (범위 정보가 있는 숫자형만) */
  static bool inRange(const proto::ParamInfo & p, const QVariant & v, QString & err);

  QList<proto::ParamInfo> params_;
  QHash<QString, int> index_;          ///< 이름 → 행 번호
  QHash<QString, QVariant> pending_;   ///< 이름 → 변경 예정 값
  QSet<QString> highlighted_;          ///< 방금 바뀐 이름
};

}  // namespace amr

#endif  // AMR_TUNER_CORE_PARAM_MODEL_HPP
