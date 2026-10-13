/**
 * @file    app_log.cpp
 * @brief   로그 구현
 */
#include "core/app_log.hpp"

#include <QDebug>

#include "core/kst_time.hpp"

namespace amr
{

AppLog & AppLog::instance()
{
  static AppLog log;
  return log;
}

void AppLog::info(const QString & tag, const QString & msg)
{
  instance().write(Level::Info, tag, msg);
}

void AppLog::warn(const QString & tag, const QString & msg)
{
  instance().write(Level::Warn, tag, msg);
}

void AppLog::error(const QString & tag, const QString & msg)
{
  instance().write(Level::Error, tag, msg);
}

QString AppLog::format(Level level, const QString & tag, const QString & msg)
{
  QString lv;
  switch (level) {
    case Level::Info:
      lv = QStringLiteral("INFO ");
      break;
    case Level::Warn:
      lv = QStringLiteral("WARN ");
      break;
    case Level::Error:
    default:
      lv = QStringLiteral("ERROR");
      break;
  }
  /* 분류는 5글자 폭(PARAM 기준)으로 맞춰 줄이 가지런하게 보이게 한다 */
  return QStringLiteral("[%1][%2][%3] %4")
         .arg(kst::nowForLog(), lv, tag.leftJustified(5, QLatin1Char(' ')), msg);
}

void AppLog::write(Level level, const QString & tag, const QString & msg)
{
  const QString line = format(level, tag, msg);
  if (Level::Error == level) {
    qWarning().noquote() << line;
  } else {
    qInfo().noquote() << line;
  }
  emit logged(line, level);
}

}  // namespace amr
