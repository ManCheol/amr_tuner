# AMR Tuner

ROS가 설치되지 않은 Windows PC에서 AMR의 **PID 파라미터를 조회·조정·기록**하는 Qt 데스크톱 앱입니다.
WSL2(또는 다른 PC)에서 실행 중인 중계 노드 [`amr_param_gateway`](../../ros2/amr_param_gateway/README.md)에 TCP로 접속합니다.

- 개발 환경: Windows 11, Qt 6.12.0 (MinGW 13.1 64-bit), C++20, CMake + Ninja
- 상대 시스템: WSL2 Ubuntu 24.04.5 LTS, ROS 2 Jazzy, `pid_goal_controller`
- 작성일: 2026-10-14 (서울 시간, UTC+09:00)

```
[Windows]  AMR Tuner ──TCP 9310 (한 줄 JSON)──▶ [WSL2] amr_param_gateway ──ROS 파라미터 서비스──▶ pid_goal_controller
               ▲                                          │
               └──────── param_changed 알림 ◀──────────────┘  (rqt·ros2 param set으로 바꾼 값도 반영)
```

## 1. 기능 (1단계)

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

다음 단계 예정: 그래프 감시(Qt Charts), 목표 지정·정지 등 제어 명령 (중계 노드 기능 추가 필요).

## 2. 사용 순서

1. WSL에서 시뮬레이션과 중계 노드 실행
   ```bash
   ros2 launch amr_pid_control pid_sim.launch.py use_safety:=false
   ros2 launch amr_param_gateway gateway.launch.py
   ```
2. AMR Tuner 실행 → 주소 `127.0.0.1`, 포트 `9310` → **[접속]**
3. "변경 값" 칸을 더블 클릭(또는 바로 숫자 입력) → Enter → 노란색 "변경 예정"
4. **[적용]** → 로그에 `apply 2 change(s): linear.kp 0.6 -> 1.0, ...` 기록, 값이 파란색으로 바뀜
5. 시험 전 **[조건 저장]** (메모에 rosbag 이름 등 기록) → 나중에 **[조건 불러오기]** → [적용]으로 같은 조건 재현

| 조작 | 동작 |
|---|---|
| 더블 클릭 / F2 / 숫자 입력 | 변경 값 편집 (범위에 맞는 입력 상자) |
| Delete | 선택한 줄의 변경 예정 취소 |
| [되돌리기] | 변경 예정 전체 취소 (노드 값은 그대로) |
| [새로 고침] | 목록·현재 값 다시 받기 (변경 예정은 유지) |

## 3. 빌드

### Qt Creator (권장)

1. **파일 > 파일 또는 프로젝트 열기 (Ctrl+O)** → 폴더가 아닌 **`CMakeLists.txt` 파일** 선택
   - **Open Workspace**나 폴더 끌어다 놓기로 열면 CMake가 없는 "Workspace 프로젝트"가 되어 빌드할 수 없음
     (`.qtcreator\project.json`이 생김 → 프로젝트를 닫고 `.qtcreator` 폴더를 지운 뒤 다시 열기)
   - 다시 열 때도 최근 프로젝트에서 경로가 `...\CMakeLists.txt`로 끝나는 항목 선택
2. 키트: **Desktop Qt 6.12.0 MinGW 64-bit** 체크 → Configure Project
3. 빌드(Ctrl+B) → 실행(Ctrl+R)
4. 단위 시험: **도구 > 테스트 > 모든 테스트 실행** (Qt Test 4종)

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
C:\Qt\6.12.0\mingw_64\bin\windeployqt.exe --release dist\amr_tuner.exe
```
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
    kst_time.*, app_log.*     서울 시간 표기, 공통 로그 형식
  ui/
    main_window.*             주 화면
    param_delegate.*          형식별 입력 상자 (실수·정수·참거짓·문자열)
    param_filter_proxy.*      검색·숨기기
tests/
  fake_gateway_server.*       가짜 중계 서버 (실제 TCP, 규약 흉내)
  test_gateway_protocol.cpp   규약
  test_param_model.cpp        모델 검사 규칙
  test_condition_file.cpp     조건 파일·서울 시간·로그 형식
  test_gateway_client.cpp     통신 (응답 대응, 시간 초과, 쪼개진 수신, 끊김·재접속, 접속 거부, 응답 없는 주소)
```

**설계 원칙**
- 통신은 모두 화면 스레드의 이벤트 루프에서 비동기 처리 (별도 스레드·잠금 없음, 화면이 멈추지 않음)
- 규약(JSON 형식)은 `gateway_protocol`에만 있음 → 규약이 바뀌면 한 파일만 수정
- 값 검사는 모델 한곳에서 → 표 편집, 조건 불러오기, 시험 코드가 같은 규칙
- 통신 응답 처리 중에는 모달 창을 띄우지 않음 (`open()` 사용, 수신 처리 중첩 방지)
- 연결이 끊기면 대기 중인 요청을 모두 실패 처리 (버튼이 잠긴 채 남지 않음)

## 6. 문제 해결

| 증상 | 확인 |
|---|---|
| 접속 중... 에서 멈춤 | WSL에서 `ss -ltnp \| grep 9310`, Windows에서 `Test-NetConnection localhost -Port 9310` |
| 연결됨 · 대상 노드 없음 | PID 노드가 꺼져 있음 → 노드 실행 후 [새로 고침] (노드가 켜지면 자동 감지) |
| 적용 거부: REJECTED | 범위 밖 또는 값 사이 관계 위반 (예: stop_distance ≥ slow_distance). 아무 값도 바뀌지 않음 |
| 접속 거부 (server_full) | 중계 노드 동시 접속 한도(기본 4) 초과 → 다른 창이나 `gw_cli.py watch` 종료 |
