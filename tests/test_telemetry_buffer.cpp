/**
 * @file    test_telemetry_buffer.cpp
 * @brief   그래프 데이터 버퍼 단위 시험 — 이동 시작·채널 값·점 줄이기·커서 값·상태 구간
 */
#include <QtTest>

#include <cmath>

#include "core/telemetry_buffer.hpp"

using amr::AppendResult;
using amr::Channel;
using amr::StateSpan;
using amr::TelemetryBuffer;
using amr::proto::TelemetrySample;

namespace
{

/** 시험용 한 주기: 직진 중, 궤적 추종, 명령 v */
TelemetrySample sample(double t, int state = 2, double v = 0.1)
{
  TelemetrySample s;
  s.t = t;
  s.state = state;
  s.hasCmd = true;
  s.cmdV = v;
  s.cmdW = 0.0;
  s.hasTraj = true;
  s.traj = {0.5, 0.2, 0.0, 0.497, 0.003, 1.0, 5.0, (2 == state) ? 1.0 : 0.0};
  s.hasLin = true;
  s.lin = {0.003, 0.015, 0.0, 0.0, 0.015, 0.0};
  s.hasErr = true;
  s.err = {1.0, 0.0174532925199, -0.0349065850399};   // 1°, −2°
  return s;
}

}  // namespace

class TestTelemetryBuffer : public QObject
{
  Q_OBJECT

private slots:
  /* 1. 키 ↔ 채널: 모든 채널이 서로 다른 키를 갖고 되돌아온다 */
  void keysRoundTrip()
  {
    QSet<QString> seen;
    for (std::size_t i = 0U; i < amr::kChannelCount; ++i) {
      const auto c = static_cast<Channel>(i);
      const QString k = TelemetryBuffer::key(c);
      QVERIFY2(false == k.isEmpty(), qPrintable(QString::number(i)));
      QVERIFY(false == seen.contains(k));
      seen.insert(k);
      Channel back = Channel::Count;
      QVERIFY(TelemetryBuffer::channelFromKey(k, back));
      QCOMPARE(back, c);
    }
    Channel c = Channel::Count;
    QVERIFY(false == TelemetryBuffer::channelFromKey(QStringLiteral("nope"), c));
  }

  /* 2. 첫 주기가 0초, 값 변환 (궤적 오차 mm, 각도 °) */
  void firstSampleStartsRunAndConverts()
  {
    TelemetryBuffer b;
    QCOMPARE(b.append(sample(100.0)), AppendResult::NewRun);
    QCOMPARE(b.append(sample(100.05)), AppendResult::Added);
    QCOMPARE(b.runNumber(), 1);
    QCOMPARE(b.size(), 2);
    QCOMPARE(b.firstTime(), 0.0);
    QVERIFY(std::fabs(b.lastTime() - 0.05) < 1e-9);
    QCOMPARE(b.latest(Channel::CmdV), 0.1);
    QCOMPARE(b.latest(Channel::TrajVRef), 0.2);
    QVERIFY(std::fabs(b.latest(Channel::TrajErrMm) - 3.0) < 1e-9);
    QVERIFY(std::fabs(b.latest(Channel::GoalHeadingDeg) - 1.0) < 1e-6);
    QVERIFY(std::fabs(b.latest(Channel::GoalYawDeg) + 2.0) < 1e-6);
    QCOMPARE(b.latest(Channel::State), 2.0);
    QVERIFY(std::isnan(b.latest(Channel::AngP)));   // 받지 않은 묶음은 NaN
  }

  /* 3. 궤적을 따라가지 않을 때(active 0)는 기준·추종 오차를 그리지 않는다 */
  void trajectoryValuesOnlyWhileActive()
  {
    TelemetryBuffer b;
    (void)b.append(sample(0.0, 1));   // 회전 중: active 0
    QVERIFY(std::isnan(b.latest(Channel::TrajVRef)));
    QVERIFY(std::isnan(b.latest(Channel::TrajErrMm)));
    QCOMPARE(b.latest(Channel::LinOut), 0.015);   // PID 항은 그대로
  }

  /* 4. 목표 알림 = 새 이동: 지우고 목표 시각이 0초, 그보다 앞선 주기는 버림 */
  void startRunOnGoal()
  {
    TelemetryBuffer b;
    (void)b.append(sample(10.0));
    (void)b.append(sample(10.05));
    b.startRun(20.0);
    QCOMPARE(b.runNumber(), 2);
    QVERIFY(b.isEmpty());
    QCOMPARE(b.append(sample(19.98)), AppendResult::Dropped);
    QCOMPARE(b.append(sample(20.02)), AppendResult::Added);
    QVERIFY(std::fabs(b.firstTime() - 0.02) < 1e-9);
  }

  /* 5. 시각이 1초 넘게 거꾸로 가면 새 이동 (시뮬레이션 재시작), 작은 역행은 버림 */
  void timeJumpBackStartsNewRun()
  {
    TelemetryBuffer b;
    (void)b.append(sample(500.0));
    (void)b.append(sample(500.05));
    QCOMPARE(b.append(sample(499.9)), AppendResult::Dropped);   // 0.15초 역행: 이동 시작 전
    QCOMPARE(b.append(sample(3.0)), AppendResult::NewRun);
    QCOMPARE(b.runNumber(), 2);
    QCOMPARE(b.size(), 1);
    QCOMPARE(b.firstTime(), 0.0);
  }

  /* 6. 보관 한도: 오래된 주기부터 버림 */
  void capacityDropsOldest()
  {
    TelemetryBuffer b(5);
    for (int i = 0; i < 8; ++i) {
      (void)b.append(sample(static_cast<double>(i)));
    }
    QCOMPARE(b.size(), 5);
    QCOMPARE(b.firstTime(), 3.0);
    QCOMPARE(b.lastTime(), 7.0);
  }

  /* 7. 커서 값: 그 시각 이전의 가장 가까운 주기, 처음보다 앞이면 NaN */
  void valueAtCursor()
  {
    TelemetryBuffer b;
    for (int i = 0; i < 5; ++i) {
      (void)b.append(sample(i * 0.1, 2, i * 0.01));
    }
    QCOMPARE(b.valueAt(Channel::CmdV, 0.25), 0.02);
    QCOMPARE(b.valueAt(Channel::CmdV, 0.2), 0.02);   // 경계는 그 주기
    QCOMPARE(b.valueAt(Channel::CmdV, 9.0), 0.04);
    QVERIFY(std::isnan(b.valueAt(Channel::CmdV, -0.1)));
    QVERIFY(std::isnan(b.valueAt(Channel::CmdV, std::nan(""))));
  }

  /* 8. 구간 점: 앞뒤 한 점 포함, NaN 제외 */
  void pointsInWindow()
  {
    TelemetryBuffer b;
    for (int i = 0; i < 10; ++i) {
      (void)b.append(sample(i * 1.0, (5 == i) ? 1 : 2, i * 1.0));
    }
    QList<QPointF> p = b.points(Channel::CmdV, 3.5, 6.5);
    QCOMPARE(p.size(), 5);   // 3, 4, 5, 6, 7
    QCOMPARE(p.first().x(), 3.0);
    QCOMPARE(p.last().x(), 7.0);
    p = b.points(Channel::TrajVRef, 3.5, 6.5);
    QCOMPARE(p.size(), 4);   // 5초는 회전 중(active 0) → NaN 제외
    QVERIFY(b.points(Channel::CmdV, 20.0, 10.0).isEmpty());
  }

  /* 9. 점 줄이기: 개수 제한, 시간 순서 유지, 튀는 값(최대)이 남는다 */
  void pointsDecimationKeepsPeaks()
  {
    TelemetryBuffer b;
    for (int i = 0; i < 10000; ++i) {
      const double v = (7777 == i) ? 50.0 : std::sin(i * 0.01);
      (void)b.append(sample(i * 0.05, 2, v));
    }
    const QList<QPointF> p = b.points(Channel::CmdV, 0.0, 500.0, 400);
    QVERIFY(p.size() <= 400);
    QVERIFY(p.size() > 300);
    double maxV = -1e9;
    for (qsizetype i = 1; i < p.size(); ++i) {
      QVERIFY(p[i].x() >= p[i - 1].x());
      maxV = std::max(maxV, p[i].y());
    }
    QCOMPARE(maxV, 50.0);
  }

  /* 10. 값 범위 (세로축 맞춤) */
  void valueRangeInWindow()
  {
    TelemetryBuffer b;
    for (int i = 0; i < 10; ++i) {
      (void)b.append(sample(i * 1.0, 2, (i - 4) * 0.1));
    }
    double lo = 0.0;
    double hi = 0.0;
    QVERIFY(b.valueRange(Channel::CmdV, 2.0, 6.0, lo, hi));
    QVERIFY(std::fabs(lo + 0.2) < 1e-9);
    QVERIFY(std::fabs(hi - 0.2) < 1e-9);
    QVERIFY(false == b.valueRange(Channel::AngP, 0.0, 9.0, lo, hi));   // 값 없음
  }

  /* 11. 상태 구간: 같은 상태는 합치고, 끝은 다음 상태 시작 시각 */
  void stateSpansMerge()
  {
    TelemetryBuffer b;
    const int states[] = {1, 1, 1, 2, 2, 2, 2, 3, 4, 4};
    for (int i = 0; i < 10; ++i) {
      (void)b.append(sample(i * 0.5, states[i]));
    }
    const QList<StateSpan> s = b.stateSpans(0.0, 10.0);
    QCOMPARE(s.size(), 4);
    QCOMPARE(s[0].state, 1);
    QCOMPARE(s[0].t0, 0.0);
    QCOMPARE(s[0].t1, 1.5);
    QCOMPARE(s[1].state, 2);
    QCOMPARE(s[1].t1, 3.5);
    QCOMPARE(s[3].state, 4);
    QCOMPARE(s[3].t1, 4.5);   // 마지막 주기 시각
    QCOMPARE(b.stateSpans(1.6, 2.4).size(), 1);   // 구간 안은 직진 하나 (앞 한 점 포함해도 같은 상태)
  }

  /* 12. 수신 주기 */
  void rateEstimate()
  {
    TelemetryBuffer b;
    QCOMPARE(b.rateHz(), 0.0);
    for (int i = 0; i <= 100; ++i) {
      (void)b.append(sample(i * 0.05));
    }
    QVERIFY(std::fabs(b.rateHz(2.0) - 20.0) < 0.1);
  }

  /* 13. 상태 이름 */
  void stateNames()
  {
    QCOMPARE(TelemetryBuffer::stateName(2), QStringLiteral("직진"));
    QCOMPARE(TelemetryBuffer::stateName(4), QStringLiteral("도착"));
    QCOMPARE(TelemetryBuffer::stateName(9), QStringLiteral("?"));
  }
};

QTEST_GUILESS_MAIN(TestTelemetryBuffer)
#include "test_telemetry_buffer.moc"
