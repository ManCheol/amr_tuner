/**
 * @file    main.cpp
 * @brief   AMR Tuner 진입점
 *
 * ROS가 설치되지 않은 Windows PC에서 실행하는 PID 파라미터 조정 앱.
 * WSL2(또는 다른 PC)의 amr_param_gateway에 TCP로 접속한다.
 */
#include <QApplication>
#include <QStyleFactory>

#include "ui/main_window.hpp"

int main(int argc, char * argv[])
{
  QApplication app(argc, argv);
  /* QSettings 저장 위치 (Windows: 레지스트리 HKCU\Software\amr\amr_tuner) */
  QApplication::setOrganizationName(QStringLiteral("amr"));
  QApplication::setApplicationName(QStringLiteral("amr_tuner"));
  QApplication::setApplicationVersion(QStringLiteral(AMR_TUNER_VERSION));
  /* Fusion: Windows 버전·테마와 관계없이 같은 모양 (표 색 표시가 일정하게 보임) */
  QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

  amr::MainWindow w;
  w.show();
  return QApplication::exec();
}
