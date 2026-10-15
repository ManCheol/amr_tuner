# AMR Tuner

ROS가 설치되지 않은 Windows PC에서 AMR의 **PID 파라미터를 조회·조정·기록**하고, **제어 결과를 실시간 그래프로 확인**하는 Qt 데스크톱 앱입니다.
WSL2(또는 다른 PC)에서 실행 중인 중계 노드 [`amr_param_gateway`](../../ros2/amr_param_gateway/README.md)에 TCP로 접속합니다.

- 개발 환경: Windows 11, Qt 6.12.0 (MinGW 13.1 64-bit), C++20, CMake + Ninja, Qt Graphs (QML)
- 상대 시스템: WSL2 Ubuntu 24.04.5 LTS, ROS 2 Jazzy, `pid_goal_controller`
- 작성일: 2026-10-14, 0.2.0(그래프): 2026-10-14 (서울 시간, UTC+09:00)

```
[Windows]  AMR Tuner ──TCP 9310 (한 줄 JSON)──▶ [WSL2] amr_param_gateway ──ROS 파라미터 서비스──▶ pid_goal_controller
               ▲                                          │
               ├──────── param_changed 알림 ◀──────────────┤  (rqt·ros2 param set으로 바꾼 값도 반영)
               └──────── telemetry·goal 알림 ◀─────────────┘  (그래프, 중계 노드 0.2.0 이상)
```

## 1. 기능

### 1단계: 파라미터

| 기능 | 설명 |
|---|---|
| 접속 / 끊기 | 주소·포트 입력 후 접속. 중계 노드가 재시작되면 **2초마다 자동 재접속**, 접속 시도는 3초 안에 끝나지 않으면 포기 후 재시도 |
| 파라미터 표 | 이름, 현재 값, 변경 값, 범위, 한글 설명. 읽기 전용(시작 시에만 읽는 값)은 회색 |
| **두 단계 변경** | 값을 고치면 노란색 "변경 예정"으로만 표시 → **[적용]** 을 눌러야 한 번에 반영 |
| 전부 적용 또는 전부 거부 | 여러 값을 한 요청으로 보냄. 하나라도 거부되면 아무것도 바뀌지 않음 (거부 사유 표시) |
| 입력 검사 | 형식·범위·읽기 전용을 앱에서 먼저 검사 (노드도 다시 검사) |
| 외부 변경 반영 | rqt 등 다른 곳에서 바뀐 값이 표에 바로 반영되고 파란색으로 잠시 표시 |
| 시험 조건 저장 | 현재 적용된 값을 메모·서울 시간과 함께 JSON 파일로 저장 |
| 시험 조건 불러오기 | 파일 값을 **변경 예정으로만** 불러옴 → 확인 후 [적용] |
| 걸러 보기 | 이름·설명 검색, ROS 내부 항목(qos_overrides 등) 숨기기, 변경 예정만 보기 |
| 로그 | `[서울 시간][수준][분류] 내용` 형식. 적용 전후 값, 거부 사유, 외부 변경 기록 |

### 2단계: 그래프 탭 (0.2.0)

| 그래프 (2열 × 3행) | 선 | 보는 것 |
|---|---|---|
| 선속도 [m/s] | 기준 v_ref, 명령 | 궤적 계획대로 가감속하는지, 명령이 튀는지 |
| 거리 PID 항 [m/s] | P, I, D, 출력 | 항별 기여, 포화 (범례에 "포화" 표시) |
| 궤적 추종 오차 [mm] | 추종 오차 (+ = 계획보다 뒤처짐) | 이득(kp)·앞먹임 효과, 위치 보정 충격 |
| 방향 PID 항 [rad/s] | P, I, D, 출력 | 회전 중 진동·포화 |
| 목표 거리 오차 [m] | 거리 | 수렴, 정지 오차 |
| 방향 오차 [°] | 목표 방향, 최종 자세 | 회전 수렴, 최종 자세 오차 |

| 기능 | 설명 |
|---|---|
| 상태 띠 | 각 그래프 위쪽 색 띠: 방향 회전·직진·최종 회전·도착·막힘 대기·막혀 포기 구간 |
| 범례 값 | 실시간이면 마지막 값, 일시정지 중에는 커서 시각의 값 |
| 목표를 받으면 새로 그리기 | RViz 2D Goal Pose 등으로 새 목표가 들어오면 그래프를 지우고 0초부터 (끄면 이어 그림) |
| 보기 | 최근 10·20·30·60초 또는 이동 전체 |
| 일시정지 (Space, 그래프 클릭) | 커서 값 표시, 휠 = 확대/축소, 끌기 = 이동, 두 번 클릭 = 이동 전체. 멈춘 동안 받은 데이터는 대기열에 두었다가 [실시간으로]를 누르면 반영 |
| 지우기 | 그래프를 비우고 다음 데이터부터 0초 |

다음 단계 예정: 목표 지정·정지 등 제어 명령 (중계 노드 기능 추가 필요).

## 2. 사용 순서

1. WSL에서 시뮬레이션과 중계 노드 실행
   ```bash
   ros2 launch amr_pid_control pid_sim.launch.py use_safety:=false
   ros2 launch amr_param_gateway gateway.launch.py use_sim_time:=true   # Gazebo: 그래프 시각 = 시뮬레이션 시계
   ```
2. AMR Tuner 실행 → 주소 `127.0.0.1`, 포트 `9310` → **[접속]**
3. "변경 값" 칸을 더블 클릭(또는 바로 숫자 입력) → Enter → 노란색 "변경 예정"
4. **[적용]** → 로그에 `apply 2 change(s): linear.kp 0.6 -> 1.0, ...` 기록, 값이 파란색으로 바뀜
5. 시험 전 **[조건 저장]** (메모에 rosbag 이름 등 기록) → 나중에 **[조건 불러오기]** → [적용]으로 같은 조건 재현
6. **[그래프]** 탭 → RViz에서 목표 지정 → 이동이 0초부터 그려짐 → 끝나면 그래프 클릭(일시정지)으로 구간별 값 확인
   → [파라미터] 탭에서 값 수정·적용 → 다시 목표 지정해 비교

| 조작 | 동작 |
|---|---|
| 더블 클릭 / F2 / 숫자 입력 | 변경 값 편집 (범위에 맞는 입력 상자) |
| Delete | 선택한 줄의 변경 예정 취소 |
| [되돌리기] | 변경 예정 전체 취소 (노드 값은 그대로) |
| [새로 고침] | 목록·현재 값 다시 받기 (변경 예정은 유지) |

## 3. 빌드

### 준비: Qt Graphs 설치 (0.2.0부터 필요)

Qt Maintenance Tool → 구성 요소 추가 → **Qt 6.12.0 > Additional Libraries > Qt Graphs** 선택
(Qt Quick 3D, Qt Shader Tools가 함께 선택됨). 설치하지 않으면 CMake 구성에서 `Qt6Graphs` 를 찾지 못합니다.
그래프 없이 핵심부·단위 시험만 빌드하려면 `-DAMR_TUNER_BUILD_APP=OFF`.

### Qt Creator (권장)

1. **파일 > 파일 또는 프로젝트 열기 (Ctrl+O)** → 폴더가 아닌 **`CMakeLists.txt` 파일** 선택
   - **Open Workspace**나 폴더 끌어다 놓기로 열면 CMake가 없는 "Workspace 프로젝트"가 되어 빌드할 수 없음
     (`.qtcreator\project.json`이 생김 → 프로젝트를 닫고 `.qtcreator` 폴더를 지운 뒤 다시 열기)
   - 다시 열 때도 최근 프로젝트에서 경로가 `...\CMakeLists.txt`로 끝나는 항목 선택
2. 키트: **Desktop Qt 6.12.0 MinGW 64-bit** 체크 → Configure Project
3. 빌드(Ctrl+B) → 실행(Ctrl+R)
4. 단위 시험: **도구 > 테스트 > 모든 테스트 실행** (Qt Test 5종)

### 명령줄 (cmd)

```bat
set PATH=C:\Qt\6.12.0\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;%PATH%
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64
cmake --build build
ctest --test-dir build --output-on-failure
build\amr_tuner.exe
```

### 배포 (다른 PC에서 실행)

```bat
mkdir dist && copy build\amr_tuner.exe dist\
C:\Qt\6.12.0\mingw_64\bin\windeployqt.exe --release --qmldir qml dist\amr_tuner.exe
```
`--qmldir qml`: QML이 쓰는 모듈(QtQuick, QtGraphs 등)을 찾아 함께 복사합니다. 빠지면 그래프 탭에 "모듈을 찾을 수 없음" 오류가 표시됩니다.
`dist` 폴더 전체를 복사하면 Qt가 없는 PC에서도 실행됩니다.

## 4. 시험 조건 파일 (`amr_tuner/1`)

```json
{
    "format": "amr_tuner/1",
    "node": "/pid_goal_controller",
    "note": "kp05_kd20, 출발 거리 2 m, bag: kp05_kd20_2",
    "params": {
        "angular.anti_windup": true,
        "linear.kd": 2,
        "linear.kp": 0.5
    },
    "saved_at": "2026-10-14T09:30:00+09:00"
}
```
- 저장 대상: 바꿀 수 있는 값만 (읽기 전용, `qos_overrides.*`, `use_sim_time` 제외)
- 불러올 때: 없는 이름·범위 밖·읽기 전용 값은 건너뛰고 사유를 표시

## 5. 구조

```
src/
  core/                       ← 화면과 무관 (단위 시험 대상)
    gateway_protocol.*        규약 v1: 요청 생성, 응답·알림 해석, 값 변환, 줄 나누기
    gateway_client.*          QTcpSocket, 요청 번호·시간 초과·접속 제한 시간·자동 재접속
    param_model.*             표 모델: 현재 값 / 변경 예정 / 검사 / 원격 반영
    condition_file.*          시험 조건 파일 저장·불러오기
    telemetry_buffer.*        그래프 데이터: 이동 시작 0초, 채널 값, 점 줄이기, 커서 값, 상태 구간
    kst_time.*, app_log.*     서울 시간 표기, 공통 로그 형식
  ui/
    main_window.*             주 화면
    param_delegate.*          형식별 입력 상자 (실수·정수·참거짓·문자열)
    param_filter_proxy.*      검색·숨기기
    telemetry_view.*          그래프 탭: 도구줄 + QQuickWidget, 일시정지 대기열
    plot_bridge.*             QML ↔ 데이터: 그래프 정의, 보이는 구간, 축 범위, 선 점 넣기(replace)
qml/                          ← Qt Graphs 화면 (qt_add_qml_module AmrTuner, 빌드 때 컴파일)
  TelemetryView.qml           상태 범례 + 그래프 6개 배치
  ChartPanel.qml              그래프 하나: 제목·범례 값, GraphsView, 상태 띠·커서·마우스 조작
tests/
  fake_gateway_server.*       가짜 중계 서버 (실제 TCP, 규약 흉내)
  test_gateway_protocol.cpp   규약
  test_param_model.cpp        모델 검사 규칙
  test_condition_file.cpp     조건 파일·서울 시간·로그 형식
  test_gateway_client.cpp     통신 (응답 대응, 시간 초과, 쪼개진 수신, 끊김·재접속, 접속 거부, 응답 없는 주소)
  test_telemetry_buffer.cpp   그래프 데이터 (이동 시작, 시각 역행, 보관 한도, 커서 값, 점 줄이기, 상태 구간)
```

**설계 원칙**
- 통신은 모두 화면 스레드의 이벤트 루프에서 비동기 처리 (별도 스레드·잠금 없음, 화면이 멈추지 않음)
- 규약(JSON 형식)은 `gateway_protocol`에만 있음 → 규약이 바뀌면 한 파일만 수정
- 값 검사는 모델 한곳에서 → 표 편집, 조건 불러오기, 시험 코드가 같은 규칙
- 통신 응답 처리 중에는 모달 창을 띄우지 않음 (`open()` 사용, 수신 처리 중첩 방지)
- 연결이 끊기면 대기 중인 요청을 모두 실패 처리 (버튼이 잠긴 채 남지 않음)
- 그래프: 무엇을 그릴지(채널·색·단위)는 C++ `PlotBridge` 한곳, 모양과 마우스 조작은 QML. 20 Hz로 받아도 다시 그리기는 50 ms 타이머에서 바뀐 것이 있을 때만, 선마다 `replace()` 한 번
- 중계 노드가 그래프 알림을 지원하는지 `hello`의 `events`로 확인 후 구독 (0.1.0 중계 노드와도 파라미터 기능은 그대로)

## 6. 문제 해결

| 증상 | 확인 |
|---|---|
| 접속 중... 에서 멈춤 | WSL에서 `ss -ltnp \| grep 9310`, Windows에서 `Test-NetConnection localhost -Port 9310` |
| 연결됨 · 대상 노드 없음 | PID 노드가 꺼져 있음 → 노드 실행 후 [새로 고침] (노드가 켜지면 자동 감지) |
| 적용 거부: REJECTED | 범위 밖 또는 값 사이 관계 위반 (예: stop_distance ≥ slow_distance). 아무 값도 바뀌지 않음 |
| 접속 거부 (server_full) | 중계 노드 동시 접속 한도(기본 4) 초과 → 다른 창이나 `gw_cli.py watch` 종료 |
| 그래프 탭: "그래프 알림을 지원하지 않음" | 중계 노드 0.1.0 → amr_param_gateway 0.2.0으로 다시 빌드 |
| 그래프 탭: "데이터 기다리는 중" | PID 노드가 꺼져 있거나 목표 전. WSL에서 `ros2 run amr_param_gateway gw_cli.py telemetry`로 중계 확인 |
| 그래프 탭: 빨간 QML 오류 | Qt Graphs 미설치 또는 배포 때 `--qmldir` 누락 |
| 그래프 시간이 궤적 시간과 어긋남 | 중계 노드를 `use_sim_time:=true`로 실행 (Gazebo가 실시간보다 느릴 때) |
