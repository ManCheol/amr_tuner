/**
 * @file    test_capture_record.cpp
 * @brief   그래프 화면 저장 기록 (파일 이름, 같은 이름 피하기, JSON 형식·조건 파일 호환) 시험
 */
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <cmath>
#include <limits>

#include "core/capture_record.hpp"
#include "core/condition_file.hpp"

using amr::CaptureInfo;
using amr::CaptureRecord;
using amr::Condition;
using amr::ConditionFile;

namespace
{
void touch(const QString & path)
{
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  (void)f.write("x");
}

CaptureInfo sampleInfo()
{
  CaptureInfo i;
  i.image = QStringLiteral("graph_20261016_093005.png");
  i.run = 3;
  i.elapsedS = 11.8504;
  i.viewFromS = 0.0;
  i.viewToS = 11.8504;
  i.paused = true;
  i.cursorS = 3.4219;
  i.hasGoal = true;
  i.goal.x = 3.41;
  i.goal.y = 0.862;
  i.goal.yaw = -1.2251485;
  i.goal.frame = QStringLiteral("map");
  i.goal.stamp = QStringLiteral("2026-10-16T09:29:50+09:00");
  i.pendingChanges = 1;
  i.gateway = QStringLiteral("0.3.0");
  return i;
}
}  // namespace

class TestCaptureRecord : public QObject
{
  Q_OBJECT

private slots:
  /* 1. 파일 이름: 서울 시간 (UTC 00:30:05 → 09:30:05) */
  void baseNameIsSeoulTime()
  {
    const QDateTime utc(QDate(2026, 10, 16), QTime(0, 30, 5), QTimeZone::UTC);
    QCOMPARE(CaptureRecord::baseName(utc), QStringLiteral("graph_20261016_093005"));
    const QDateTime late(QDate(2026, 10, 16), QTime(15, 0, 0), QTimeZone::UTC);
    QCOMPARE(CaptureRecord::baseName(late), QStringLiteral("graph_20261017_000000"));   // 날짜 넘어감
  }

  /* 2. 같은 이름이 있으면 _2, _3 … (PNG·JSON 어느 쪽이든) */
  void uniqueBaseSkipsExisting()
  {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QDir dir(tmp.path());
    const QString base = QStringLiteral("graph_20261016_093005");
    QCOMPARE(CaptureRecord::uniqueBase(dir, base), base);
    touch(dir.filePath(base + QStringLiteral(".png")));
    QCOMPARE(CaptureRecord::uniqueBase(dir, base), base + QStringLiteral("_2"));
    touch(dir.filePath(base + QStringLiteral("_2.json")));                // JSON만 있어도 피함
    QCOMPARE(CaptureRecord::uniqueBase(dir, base), base + QStringLiteral("_3"));
  }

  /* 3. JSON: 조건 파일 형식 + capture 항목, [조건 불러오기]로 그대로 읽힘 */
  void jsonIsLoadableCondition()
  {
    Condition c;
    c.node = QStringLiteral("/pid_goal_controller");
    c.savedAt = QStringLiteral("2026-10-16T09:30:05+09:00");
    c.note = QStringLiteral("path_D 3번째 이동, SLAM 보정 직후");
    c.params = QJsonObject{{QStringLiteral("linear.kp"), 5.0}, {QStringLiteral("path.enabled"), true}};
    const QByteArray data = CaptureRecord::toJson(c, sampleInfo());

    Condition back;
    QString err;
    QVERIFY2(ConditionFile::fromJson(data, back, err), qPrintable(err));
    QCOMPARE(back.note, c.note);
    QCOMPARE(back.savedAt, c.savedAt);
    QCOMPARE(back.params, c.params);

    const QJsonObject cap = QJsonDocument::fromJson(data).object().value(QStringLiteral("capture")).toObject();
    QCOMPARE(cap.value(QStringLiteral("image")).toString(), QStringLiteral("graph_20261016_093005.png"));
    QCOMPARE(cap.value(QStringLiteral("run")).toInt(), 3);
    QCOMPARE(cap.value(QStringLiteral("elapsed_s")).toDouble(), 11.85);      // 소수 3자리
    QCOMPARE(cap.value(QStringLiteral("cursor_s")).toDouble(), 3.422);
    QCOMPARE(cap.value(QStringLiteral("view_s")).toArray().size(), 2);
    QCOMPARE(cap.value(QStringLiteral("paused")).toBool(), true);
    QCOMPARE(cap.value(QStringLiteral("goal")).toObject().value(QStringLiteral("yaw")).toDouble(), -1.225);
    QCOMPARE(cap.value(QStringLiteral("pending_changes")).toInt(), 1);
    QCOMPARE(cap.value(QStringLiteral("gateway")).toString(), QStringLiteral("0.3.0"));
  }

  /* 4. 값이 없으면 null (데이터 없음·커서 없음·목표 전) */
  void missingValuesAreNull()
  {
    CaptureInfo i;   // 기본값: NaN, 목표 없음
    const QJsonObject cap = CaptureRecord::captureObject(i);
    QVERIFY(cap.value(QStringLiteral("elapsed_s")).isNull());
    QVERIFY(cap.value(QStringLiteral("cursor_s")).isNull());
    QVERIFY(cap.value(QStringLiteral("goal")).isNull());
    QVERIFY(cap.value(QStringLiteral("view_s")).toArray().at(0).isNull());
  }

  /* 5. 파일로 저장 → 조건 파일로 불러오기 */
  void saveAndLoad()
  {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("graph_20261016_093005.json"));
    Condition c;
    c.note = QStringLiteral("메모 없음 시험");
    QString err;
    QVERIFY2(CaptureRecord::save(path, c, sampleInfo(), err), qPrintable(err));
    Condition back;
    QVERIFY2(ConditionFile::load(path, back, err), qPrintable(err));
    QCOMPARE(back.note, c.note);
    QVERIFY(false == back.savedAt.isEmpty());   // 비어 있으면 저장 시각으로 채워짐
  }
};

QTEST_GUILESS_MAIN(TestCaptureRecord)
#include "test_capture_record.moc"
