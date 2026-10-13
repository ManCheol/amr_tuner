cmake_minimum_required(VERSION 3.21)

project(amr_tuner VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# C++20, Qt 6 (개발: Qt 6.12.0 MinGW 13.1 64-bit, Windows 11)
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()   # AUTOMOC 켜기 등 Qt 권장 설정

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
elseif(MSVC)
  add_compile_options(/W4 /utf-8)   # 소스의 한글 문자열을 UTF-8로 읽게 함
endif()

# ---------------------------------------------------------------------------
# 1. 핵심부 라이브러리 (통신·규약·모델·조건 파일)
#    화면(Widgets)에 의존하지 않으므로 단위 시험에서 그대로 사용한다.
# ---------------------------------------------------------------------------
add_library(amr_tuner_core STATIC
  src/core/app_log.cpp            src/core/app_log.hpp
  src/core/condition_file.cpp     src/core/condition_file.hpp
  src/core/gateway_client.cpp     src/core/gateway_client.hpp
  src/core/gateway_protocol.cpp   src/core/gateway_protocol.hpp
  src/core/kst_time.cpp           src/core/kst_time.hpp
  src/core/param_model.cpp        src/core/param_model.hpp
)
target_include_directories(amr_tuner_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(amr_tuner_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)

# ---------------------------------------------------------------------------
# 2. 앱
# ---------------------------------------------------------------------------
qt_add_executable(amr_tuner WIN32 MACOSX_BUNDLE
  src/main.cpp
  src/ui/main_window.cpp          src/ui/main_window.hpp
  src/ui/param_delegate.cpp       src/ui/param_delegate.hpp
  src/ui/param_filter_proxy.cpp   src/ui/param_filter_proxy.hpp
)
target_compile_definitions(amr_tuner PRIVATE AMR_TUNER_VERSION="${PROJECT_VERSION}")
target_link_libraries(amr_tuner PRIVATE amr_tuner_core Qt6::Widgets)

include(GNUInstallDirs)
install(TARGETS amr_tuner
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---------------------------------------------------------------------------
# 3. 단위 시험 (Qt Test). Qt Creator: 도구 > 테스트 > 모든 테스트 실행, 또는 ctest
# ---------------------------------------------------------------------------
option(AMR_TUNER_BUILD_TESTS "Build unit tests" ON)
if(AMR_TUNER_BUILD_TESTS)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  enable_testing()

  # 시험용 가짜 중계 서버 (실제 TCP로 규약을 흉내 냄)
  add_library(fake_gateway STATIC tests/fake_gateway_server.cpp tests/fake_gateway_server.hpp)
  target_include_directories(fake_gateway PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_link_libraries(fake_gateway PUBLIC amr_tuner_core Qt6::Network)

  foreach(t test_gateway_protocol test_param_model test_condition_file test_gateway_client)
    qt_add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE amr_tuner_core fake_gateway Qt6::Test)
    add_test(NAME ${t} COMMAND ${t})
    # 화면 없는 환경(CI 등)에서도 돌도록 offscreen 플랫폼 사용
    set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endforeach()
endif()
