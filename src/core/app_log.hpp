/**
 * @file    app_log.hpp
 * @brief   앱 전체에서 쓰는 한 가지 로그 형식
 *
 * 형식: [2026-10-14 09:30:00.123][INFO ][NET  ] 메시지
 *   - 시각  : 서울 시간, 밀리초까지
 *   - 수준  : INFO / WARN / ERROR
 *   - 분류  : NET(통신), PARAM(파라미터), FILE(조건 파일), UI(화면)
 *
 * 로그 한 줄은 logged 신호로 화면의 로그 창에 전달되고, 동시에 qInfo 등으로도
 * 출력되어 Qt Creator의 "응용 프로그램 출력" 창에서도 볼 수 있다.
 * 화면 스레드에서만 사용한다 (통신도 같은 스레드의 이벤트 루프에서 처리됨).
 */
#ifndef AMR_TUNER_CORE_APP_LOG_HPP
#define AMR_TUNER_CORE_APP_LOG_HPP

#include <QObject>
#include <QString>

namespace amr
{

class AppLog : public QObject
{
  Q_OBJECT

public:
  enum class Level : quint8 { Info = 0U, Warn = 1U, Error = 2U };

  /** 앱 전체에서 하나만 쓰는 객체 */
  static AppLog & instance();

  static void info(const QString & tag, const QString & msg);
  static void warn(const QString & tag, const QString & msg);
  static void error(const QString & tag, const QString & msg);

  /** 한 줄 형식 만들기 (시험에서 형식 확인용으로도 사용) */
  static QString format(Level level, const QString & tag, const QString & msg);

signals:
  /** 완성된 로그 한 줄과 수준 */
  void logged(const QString & line, amr::AppLog::Level level);

private:
  AppLog() = default;
  void write(Level level, const QString & tag, const QString & msg);
};

}  // namespace amr

#endif  // AMR_TUNER_CORE_APP_LOG_HPP
