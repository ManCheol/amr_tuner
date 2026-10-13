/**
 * @file    test_param_model.cpp
 * @brief   파라미터 모델 시험 — 변경 예정 검사, 원격 반영, 조건 불러오기
 */
#include <QSignalSpy>
#include <QtTest>

#include "core/param_model.hpp"
#include "fake_gateway_server.hpp"

using amr::ParamModel;
using namespace amr::proto;

class TestParamModel : public QObject
{
  Q_OBJECT

private:
  ParamModel model_;

  QModelIndex idx(const QString & name, int col) const
  {
    return model_.index(model_.rowOf(name), col);
  }

private slots:
  void init()
  {
    /* 매 시험마다 가짜 서버의 기본 표로 새로 시작 */
    amr::test::FakeGatewayServer srv;
    model_.clear();
    model_.setParams(srv.params());
  }

  void rowsAndDisplay()
  {
    QCOMPARE(model_.rowCount(), 8);
    QCOMPARE(model_.columnCount(), static_cast<int>(ParamModel::ColumnCount));
    QCOMPARE(idx(QStringLiteral("linear.kp"), ParamModel::ColCurrent).data().toString(), QStringLiteral("0.6"));
    QCOMPARE(idx(QStringLiteral("linear.kp"), ParamModel::ColRange).data().toString(), QStringLiteral("0 ~ 10"));
    QCOMPARE(idx(QStringLiteral("control_rate_hz"), ParamModel::ColPending).data().toString(),
      QStringLiteral("읽기 전용"));
  }

  void onlyWritablePendingCellIsEditable()
  {
    QVERIFY(idx(QStringLiteral("linear.kp"), ParamModel::ColPending).flags() & Qt::ItemIsEditable);
    QVERIFY(false == bool(idx(QStringLiteral("linear.kp"), ParamModel::ColCurrent).flags() & Qt::ItemIsEditable));
    QVERIFY(false == bool(idx(QStringLiteral("odom_topic"), ParamModel::ColPending).flags() & Qt::ItemIsEditable));
  }

  void setPendingConvertsText()
  {
    QString err;
    QVERIFY2(model_.setPending(QStringLiteral("linear.kp"), QStringLiteral(" 0.8 "), err), qPrintable(err));
    QVERIFY(model_.setPending(QStringLiteral("angular.anti_windup"), QStringLiteral("false"), err));
    QCOMPARE(model_.pendingCount(), 2);
    const QJsonObject j = model_.pendingJson();
    QVERIFY(j.value(QStringLiteral("linear.kp")).isDouble());
    QCOMPARE(j.value(QStringLiteral("linear.kp")).toDouble(), 0.8);
    QCOMPARE(j.value(QStringLiteral("angular.anti_windup")).toBool(), false);
    QVERIFY(idx(QStringLiteral("linear.kp"), 0).data(ParamModel::PendingRole).toBool());
  }

  void setPendingRejects_data()
  {
    QTest::addColumn<QString>("name");
    QTest::addColumn<QVariant>("value");
    QTest::addColumn<QString>("reasonPart");
    QTest::newRow("out of range") << QStringLiteral("linear.kp") << QVariant(QStringLiteral("11")) << QStringLiteral("범위 밖");
    QTest::newRow("negative") << QStringLiteral("linear.kp") << QVariant(-0.1) << QStringLiteral("범위 밖");
    QTest::newRow("output limit") << QStringLiteral("linear.output_limit") << QVariant(0.3) << QStringLiteral("0.26");
    QTest::newRow("read only") << QStringLiteral("control_rate_hz") << QVariant(50.0) << QStringLiteral("읽기 전용");
    QTest::newRow("not number") << QStringLiteral("linear.kp") << QVariant(QStringLiteral("abc")) << QStringLiteral("실수");
    QTest::newRow("bad bool") << QStringLiteral("angular.anti_windup") << QVariant(QStringLiteral("yes")) << QStringLiteral("true");
    QTest::newRow("unknown") << QStringLiteral("linear.kq") << QVariant(1.0) << QStringLiteral("없는");
  }
  void setPendingRejects()
  {
    QFETCH(QString, name);
    QFETCH(QVariant, value);
    QFETCH(QString, reasonPart);
    QString err;
    QVERIFY(false == model_.setPending(name, value, err));
    QVERIFY2(err.contains(reasonPart), qPrintable(err));
    QCOMPARE(model_.pendingCount(), 0);
  }

  void rangeEdgesAccepted()
  {
    QString err;
    QVERIFY(model_.setPending(QStringLiteral("linear.output_limit"), 0.26, err));
    QVERIFY(model_.setPending(QStringLiteral("linear.kp"), 0.0, err));
  }

  void sameAsCurrentClearsPending()
  {
    QString err;
    QVERIFY(model_.setPending(QStringLiteral("linear.kp"), 0.9, err));
    QCOMPARE(model_.pendingCount(), 1);
    QVERIFY(model_.setPending(QStringLiteral("linear.kp"), 0.6, err));   // 현재 값으로 되돌림
    QCOMPARE(model_.pendingCount(), 0);
  }

  void setDataReportsRejection()
  {
    QSignalSpy spy(&model_, &ParamModel::editRejected);
    QVERIFY(false == model_.setData(idx(QStringLiteral("linear.kp"), ParamModel::ColPending), 99.0));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("linear.kp"));
    QVERIFY(model_.setData(idx(QStringLiteral("linear.kp"), ParamModel::ColPending), 1.2));
  }

  void applyRemoteUpdatesAndClearsMatchingPending()
  {
    QString err;
    QVERIFY(model_.setPending(QStringLiteral("linear.kp"), 1.0, err));
    QVERIFY(model_.setPending(QStringLiteral("linear.kd"), 0.5, err));
    QSignalSpy spy(&model_, &ParamModel::pendingCountChanged);

    /* kp는 우리가 보낸 값으로 적용됨, kd는 다른 곳(rqt)에서 다른 값으로 바뀜 */
    const QStringList changed = model_.applyRemote(QJsonObject{
        {QStringLiteral("linear.kp"), 1.0}, {QStringLiteral("linear.kd"), 0.2},
        {QStringLiteral("not.in.list"), 3.0}});
    QCOMPARE(changed.size(), 2);
    QCOMPARE(model_.find(QStringLiteral("linear.kp"))->value.toDouble(), 1.0);
    QCOMPARE(model_.pendingCount(), 1);   // kp 완료, kd(0.5)는 아직 변경 예정
    QVERIFY(model_.pendingJson().contains(QStringLiteral("linear.kd")));
    QVERIFY(spy.count() >= 1);
    QVERIFY(idx(QStringLiteral("linear.kp"), ParamModel::ColCurrent).data(Qt::BackgroundRole).isValid());
    model_.clearHighlights();
    QVERIFY(false == idx(QStringLiteral("linear.kp"), ParamModel::ColCurrent).data(Qt::BackgroundRole).isValid());
  }

  void applyRemoteIntegerLiteralForDouble()
  {
    (void)model_.applyRemote(QJsonObject{{QStringLiteral("linear.kp"), 2}});
    QCOMPARE(model_.find(QStringLiteral("linear.kp"))->value.toDouble(), 2.0);
  }

  void refreshKeepsPendingWhenStillValid()
  {
    QString err;
    QVERIFY(model_.setPending(QStringLiteral("linear.kp"), 1.5, err));
    QVERIFY(model_.setPending(QStringLiteral("linear.kd"), 0.3, err));
    /* 새 목록: kd는 이미 0.3이 되어 있음 → kd 변경 예정은 사라져야 함 */
    amr::test::FakeGatewayServer srv;
    QList<ParamInfo> fresh = srv.params();
    for (ParamInfo & p : fresh) {
      if (QLatin1String("linear.kd") == p.name) {
        p.value = 0.3;
      }
    }
    model_.setParams(fresh);
    QCOMPARE(model_.pendingCount(), 1);
    QVERIFY(model_.pendingJson().contains(QStringLiteral("linear.kp")));
  }

  void savableExcludesReadOnlyAndInternal()
  {
    const QJsonObject s = model_.savableValues();
    QVERIFY(s.contains(QStringLiteral("linear.kp")));
    QVERIFY(s.contains(QStringLiteral("angular.anti_windup")));
    QVERIFY(false == s.contains(QStringLiteral("control_rate_hz")));   // 읽기 전용
    QVERIFY(false == s.contains(QStringLiteral("odom_topic")));        // 읽기 전용
    QVERIFY(false == s.contains(QStringLiteral("use_sim_time")));      // ROS 내부
    QVERIFY(false == s.contains(QStringLiteral("qos_overrides./parameter_events.publisher.depth")));
    QCOMPARE(s.size(), 4);   // anti_windup, kd, kp, output_limit
  }

  void loadPendingCountsAndSkips()
  {
    const ParamModel::LoadResult r = model_.loadPending(QJsonObject{
        {QStringLiteral("linear.kp"), 0.5},              // 변경 예정
        {QStringLiteral("linear.kd"), 2},                // 정수 표기 → 실수 2.0
        {QStringLiteral("linear.output_limit"), 0.22},   // 현재와 같음
        {QStringLiteral("linear.output_limit_x"), 1.0},  // 없는 이름
        {QStringLiteral("control_rate_hz"), 30.0},       // 읽기 전용
        {QStringLiteral("angular.anti_windup"), false}});
    QCOMPARE(r.staged, 3);
    QCOMPARE(r.unchanged, 1);
    QCOMPARE(r.skipped.size(), 2);
    QCOMPARE(model_.pendingJson().value(QStringLiteral("linear.kd")).toDouble(), 2.0);
  }

  void internalNames()
  {
    QVERIFY(ParamModel::isInternal(QStringLiteral("qos_overrides./parameter_events.publisher.depth")));
    QVERIFY(ParamModel::isInternal(QStringLiteral("use_sim_time")));
    QVERIFY(false == ParamModel::isInternal(QStringLiteral("linear.kp")));
  }

  void clearAllPending()
  {
    QString err;
    QVERIFY(model_.setPending(QStringLiteral("linear.kp"), 1.5, err));
    QSignalSpy spy(&model_, &ParamModel::pendingCountChanged);
    model_.clearAllPending();
    QCOMPARE(model_.pendingCount(), 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);
  }
};

QTEST_MAIN(TestParamModel)
#include "test_param_model.moc"
