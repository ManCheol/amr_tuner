/**
 * @file    gateway_protocol.hpp
 * @brief   amr_param_gateway 규약 v1 (앱 쪽 구현, 화면·소켓과 무관)
 *
 * ---------------------------------------------------------------------------
 * [규약 요약] (중계 노드 README와 같은 내용)
 *   - TCP, UTF-8, 한 줄에 JSON 하나 ('\n'으로 끝남, 최대 64 KB)
 *   - 요청 : {"v":1,"id":N,"type":"req","cmd":"list", ...}
 *   - 응답 : {"v":1,"id":N,"type":"res","ok":true, ...}
 *            {"v":1,"id":N,"type":"res","ok":false,"error":{"code":"REJECTED","message":"..."}}
 *   - 알림 : {"v":1,"type":"evt","evt":"param_changed","node":"...","params":{...},"stamp":"..."}
 *            {"v":1,"type":"evt","evt":"telemetry","t":12.3,"state":2,"cmd":[..],"lin":[..],...}  (중계 0.2.0~)
 *            {"v":1,"type":"evt","evt":"goal","t":12.2,"x":1,"y":0,"yaw":0,"frame":"map",...}    (중계 0.2.0~)
 *
 * [이 파일의 역할]
 *   통신(GatewayClient)과 화면(ParamModel)이 JSON 형식을 직접 다루지 않도록
 *   요청 만들기, 응답 해석, 값 변환을 한곳에 모은다. 규약이 바뀌면 이 파일만 고친다.
 * ---------------------------------------------------------------------------
 */
#ifndef AMR_TUNER_CORE_GATEWAY_PROTOCOL_HPP
#define AMR_TUNER_CORE_GATEWAY_PROTOCOL_HPP

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QVariant>

#include <array>

namespace amr::proto
{

inline constexpr int kProtocolVersion = 1;
inline constexpr qsizetype kMaxLineBytes = 64 * 1024;

/** 파라미터 형식 (중계 노드의 "type" 문자열과 대응) */
enum class ParamType : quint8
{
  Bool = 0U,
  Int = 1U,
  Double = 2U,
  String = 3U,
  Unsupported = 4U   ///< 배열 등. 화면에 표시만 하고 편집하지 않음
};

ParamType typeFromString(const QString & s);
QString typeToString(ParamType t);

/** list 응답의 항목 하나 */
struct ParamInfo
{
  QString name;
  ParamType type{ParamType::Unsupported};
  QVariant value;          ///< bool / qint64 / double / QString (Unsupported면 무효)
  bool readOnly{false};    ///< 시작할 때만 읽는 값
  QString description;
  bool hasRange{false};
  double min{0.0};
  double max{0.0};
  double step{0.0};        ///< 0이면 연속값
};

/** 받은 한 줄을 해석한 결과 */
struct Message
{
  enum class Kind : quint8 { Response = 0U, Event = 1U, Invalid = 2U };

  Kind kind{Kind::Invalid};
  qint64 id{-1};           ///< 응답일 때 요청 번호
  bool ok{false};          ///< 응답 성공 여부
  QString errorCode;       ///< 실패 시 오류 코드 (BAD_REQUEST, REJECTED ...)
  QString errorMessage;    ///< 실패 시 사유
  QString event;           ///< 알림 이름 (param_changed, server_full ...)
  QJsonObject body;        ///< 전체 객체 (명령별 항목을 꺼내 쓰기 위함)
  QString parseError;      ///< Invalid일 때 이유
};

/** 요청 한 줄 만들기 (끝에 '\n' 포함). body의 항목이 공통 머리와 합쳐진다. */
QByteArray makeRequest(qint64 id, const QString & cmd, const QJsonObject & body = QJsonObject());

/** 받은 한 줄 해석 */
Message parseMessage(const QByteArray & line);

/** list 응답의 "params" 배열 → ParamInfo 목록. 형식이 틀리면 false와 이유 */
bool parseParamList(const QJsonObject & body, QList<ParamInfo> & out, QString & err);

/**
 * JSON 값 → 형식에 맞는 QVariant. 형식이 맞지 않으면 무효 QVariant.
 * 실수 파라미터에 정수 JSON(예: 1)이 와도 1.0으로 받는다 (중계 노드와 같은 규칙).
 */
QVariant fromJson(const QJsonValue & v, ParamType type);

/** QVariant → JSON 값 (형식에 맞게) */
QJsonValue toJson(const QVariant & v, ParamType type);

/** 화면 표시용 문자열 (실수는 유효 숫자 6자리, 참/거짓은 true/false) */
QString displayValue(const QVariant & v, ParamType type);

/** 두 값이 같은지 (실수는 아주 작은 차이를 같게 봄) */
bool sameValue(const QVariant & a, const QVariant & b, ParamType type);

/* ===========================================================================
 * 그래프용 알림 (중계 노드 0.2.0~)
 * =========================================================================== */

/** 알림 이름 */
inline constexpr const char * kEvtParamChanged = "param_changed";
inline constexpr const char * kEvtTelemetry = "telemetry";
inline constexpr const char * kEvtGoal = "goal";

/** PID 항 배열 [error, p, i, d, output, saturated] */
inline constexpr qsizetype kPidTermsSize = 6;
/** 궤적 배열 [s_ref, v_ref, a_ref, s_act, error, t, T, active] */
inline constexpr qsizetype kTrajectorySize = 8;
/** 오차 배열 [distance, heading, yaw] */
inline constexpr qsizetype kErrorCount = 3;
/** 경로 추종 배열 [lateral, heading, kappa, v_limit, s, length, active] (중계 노드 0.3.0~) */
inline constexpr qsizetype kPathTrackingSize = 7;

/**
 * telemetry 알림 하나 = PID 노드의 한 제어 주기.
 * 중계 노드가 아직 받지 못한 묶음은 has* = false. 값이 null이면 NaN.
 */
struct TelemetrySample
{
  double t{0.0};                 ///< 중계 노드 시계 [s] (차이만 의미 있음)
  int state{0};                  ///< 0 IDLE, 1 ROTATE_TO_HEADING, 2 MOVE, 3 ROTATE_TO_YAW, 4 REACHED, 5 BLOCKED, 6 ABORTED
  bool hasCmd{false};
  double cmdV{0.0};              ///< 명령 선속도 [m/s]
  double cmdW{0.0};              ///< 명령 각속도 [rad/s]
  bool hasLin{false};
  std::array<double, kPidTermsSize> lin{};     ///< 거리 PID [error m, p, i, d, output m/s, saturated]
  bool hasAng{false};
  std::array<double, kPidTermsSize> ang{};     ///< 방향 PID [error rad, p, i, d, output rad/s, saturated]
  bool hasTraj{false};
  std::array<double, kTrajectorySize> traj{};  ///< 직선 궤적 [s_ref, v_ref, a_ref, s_act, error, t, T, active]
  bool hasErr{false};
  std::array<double, kErrorCount> err{};       ///< 목표 오차 [거리 m, heading rad, yaw rad]
  bool hasPath{false};                         ///< 0.2.0 중계 노드는 보내지 않음 → false
  std::array<double, kPathTrackingSize> path{};  ///< 경로 추종 [가로 m(+ = 왼쪽), 방향 rad, κ 1/m, 속도 상한 m/s, s m, 길이 m, active]
};

/** goal 알림 (PID 노드가 받은 목표) */
struct GoalInfo
{
  double t{0.0};
  double x{0.0};
  double y{0.0};
  double yaw{0.0};
  QString frame;
  QString stamp;   ///< 서울 시각
};

/**
 * telemetry 알림 본문 해석.
 * t·state가 없거나 배열 길이가 짧으면 false와 이유 (그 줄은 버린다).
 */
bool parseTelemetry(const QJsonObject & body, TelemetrySample & out, QString & err);

/** goal 알림 본문 해석. t·x·y가 없으면 false */
bool parseGoal(const QJsonObject & body, GoalInfo & out, QString & err);

/**
 * hello 응답에서 중계 노드가 지원하는 알림 목록.
 * 0.1.0은 events 항목이 없으므로 param_changed 하나로 본다.
 */
QStringList supportedEvents(const QJsonObject & helloBody);

/* ===========================================================================
 * 줄 나누기: TCP로 받은 바이트를 '\n' 단위로 자른다.
 * TCP는 보낸 단위를 지켜 주지 않으므로 한 줄이 여러 번에 나눠 오거나
 * 여러 줄이 한 번에 올 수 있다.
 * =========================================================================== */
class LineBuffer
{
public:
  explicit LineBuffer(qsizetype maxLine = kMaxLineBytes);

  /** 받은 바이트 추가 → 완성된 줄 목록 ('\r' 제거, 빈 줄 제외) */
  QList<QByteArray> append(const QByteArray & data);

  /** 줄바꿈 없이 최대 길이를 넘었는지 (비정상 상대: 연결을 끊어야 함) */
  bool overflow() const {return overflow_;}

  void clear();

private:
  qsizetype maxLine_;
  QByteArray buffer_;
  bool overflow_{false};
};

}  // namespace amr::proto

#endif  // AMR_TUNER_CORE_GATEWAY_PROTOCOL_HPP
