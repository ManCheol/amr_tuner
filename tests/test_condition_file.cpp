/**
 * @file    test_condition_file.cpp
 * @brief   조건 파일·서울 시간·로그 형식 시험
 */
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "core/app_log.hpp"
#include "core/condition_file.hpp"
#include "core/kst_time.hpp"

using amr::Condition;
using amr::ConditionFile;

class TestConditionFile : public QObject
{
  Q_OBJECT

private slots:
  void kstIsUtcPlus9()
  {
    const QDateTime utc(QDate(2026, 1, 1), QTime(0, 0, 0), QTimeZone::UTC);
    QCOMPARE(amr::kst::toIso(utc), QStringLiteral("2026-01-01T09:00:00+09:00"));
    const QDateTime late(QDate(2026, 1, 1), QTime(20, 30, 0), QTimeZone::UTC);
    QCOMPARE(amr::kst::toIso(late), QStringLiteral("2026-01-02T05:30:00+09:00"));   // 날짜 넘어감
  }

  void roundTripInMemory()
  {
    Condition c;
    c.node = QStringLiteral("/pid_goal_controller");
    c.note = QStringLiteral("kp05_kd20 시험, 출발 2 m");
    c.params = QJsonObject{{QStringLiteral("linear.kp"), 0.5}, {QStringLiteral("linear.kd"), 2.0},
      {QStringLiteral("angular.anti_windup"), true}};
    const QByteArray data = ConditionFile::toJson(c);

    Condition back;
    QString err;
    QVERIFY2(ConditionFile::fromJson(data, back, err), qPrintable(err));
    QCOMPARE(back.node, c.node);
    QCOMPARE(back.note, c.note);
    QCOMPARE(back.params, c.params);
    /* 저장 시각이 비어 있었으면 서울 시간으로 채워진다 */
    QVERIFY(QRegularExpression(QStringLiteral("^\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d\\+09:00$"))
      .match(back.savedAt).hasMatch());
    QVERIFY(data.contains("\"format\": \"amr_tuner/1\""));
  }

  void saveAndLoadFile()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("조건_한글이름.json"));   // 한글 경로
    Condition c;
    c.node = QStringLiteral("/n");
    c.savedAt = QStringLiteral("2026-10-14T09:30:00+09:00");
    c.params = QJsonObject{{QStringLiteral("linear.kp"), 1.0}};
    QString err;
    QVERIFY2(ConditionFile::save(path, c, err), qPrintable(err));
    Condition back;
    QVERIFY2(ConditionFile::load(path, back, err), qPrintable(err));
    QCOMPARE(back.savedAt, c.savedAt);   // 지정한 시각은 그대로 유지
    QCOMPARE(back.params.value(QStringLiteral("linear.kp")).toDouble(), 1.0);
  }

  void rejectsBadFiles_data()
  {
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("reasonPart");
    QTest::newRow("broken") << QByteArray("{") << QStringLiteral("JSON");
    QTest::newRow("array") << QByteArray("[]") << QStringLiteral("객체");
    QTest::newRow("format") << QByteArray(R"({"format":"other/1","params":{}})") << QStringLiteral("형식");
    QTest::newRow("no params") << QByteArray(R"({"format":"amr_tuner/1"})") << QStringLiteral("params");
  }
  void rejectsBadFiles()
  {
    QFETCH(QByteArray, data);
    QFETCH(QString, reasonPart);
    Condition c;
    QString err;
    QVERIFY(false == ConditionFile::fromJson(data, c, err));
    QVERIFY2(err.contains(reasonPart), qPrintable(err));
  }

  void loadMissingFile()
  {
    Condition c;
    QString err;
    QVERIFY(false == ConditionFile::load(QStringLiteral("/no/such/file.json"), c, err));
    QVERIFY(false == err.isEmpty());
  }

  void defaultFileName()
  {
    QVERIFY(QRegularExpression(QStringLiteral("^cond_\\d{8}_\\d{6}\\.json$"))
      .match(ConditionFile::defaultFileName()).hasMatch());
  }

  void logFormat()
  {
    const QString line = amr::AppLog::format(amr::AppLog::Level::Warn, QStringLiteral("NET"),
      QStringLiteral("socket error"));
    QVERIFY2(QRegularExpression(QStringLiteral(
        "^\\[\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d\\.\\d{3}\\]\\[WARN \\]\\[NET  \\] socket error$"))
      .match(line).hasMatch(), qPrintable(line));
  }
};

QTEST_GUILESS_MAIN(TestConditionFile)
#include "test_condition_file.moc"
