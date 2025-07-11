#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#define USE_ARDUINO_INTERRUPTS true
#include <PulseSensorPlayground.h>

// ======== 라이브러리 및 상수 정의 ========
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// 서보 펄스 길이 상수 (서보 모델에 맞게 조절 가능)
#define SERVO_MIN_MG996 150
#define SERVO_MAX_MG996 600
#define SERVO_MIN_SG90  150
#define SERVO_MAX_SG90  600

// PCA9685 서보 채널 정의
#define BASE_SERVO          0
#define SHOULDER_SERVO      1
#define ELBOW_SERVO         2
#define WRIST_ROT_SERVO     3
#define WRIST_PITCH_SERVO   4
#define GRIPPER_SERVO       5
#define SINGLE_ARM_SERVO    6
#define SG90_SERVO_1        7
#define SG90_SERVO_2        8
#define SG90_SERVO_3        9
// ==========================================================

// Wi-Fi 설정
const char* ssid = "Akashic";
const char* passwd = "microsoft";

// PCA9685 객체 생성
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// LED 설정
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

// 서보 모터 설정
Servo myservo;
const int SERVO_PIN = 17;
int currentServoAngle = 90;

int count=0; 

// Pulse Sensor 설정
const int PULSE_SENSOR_PIN = 5;
const int PULSE_SENSOR_THRESHOLD = 550;
PulseSensorPlayground pulseSensor;
int currentBPM = 0;

WebServer server(80);

// 작업 중복 실행을 막기 위한 전역 변수
bool g_isTaskRunning = false;

struct DirectPosition {
  int baseAngle;
  int shoulderAngle;
  int elbowAngle;
  int wristRotAngle;
  int wristAngle;
  int gripperAngle;
};

// ======== 위치 값 정의 ========
DirectPosition homePosition       = {90, 75, 90, 90, 90, 90};
DirectPosition clothPosition      = {120, 75, 140, 130, 170, 40};
DirectPosition liftPosition       = {120, 65, 120, 110, 155, 130};
DirectPosition deskCleanStart     = {100, 75, 140, 145, 170, 130};
DirectPosition deskCleanEnd       = {20,  75, 140, 145, 180, 130};  


// ========== SG90 제어 관련 변수 ==========
int sg90CallCount = 0;

// ========== 서보 각도 설정 함수 ==========
void setAngleMG996(uint8_t channel, float angle) {
  angle = constrain(angle, 0, 180);
  uint16_t pulse = map(angle, 0, 180, SERVO_MIN_MG996, SERVO_MAX_MG996);
  pwm.setPWM(channel, 0, pulse);
}

void setAngleSG90(uint8_t channel, float angle) {
  angle = constrain(angle, 0, 180);
  uint16_t pulse = map(angle, 0, 180, SERVO_MIN_SG90, SERVO_MAX_SG90);
  pwm.setPWM(channel, 0, pulse);
}

// ========== 직접 각도 제어 함수 ==========
void moveToDirectPosition(DirectPosition pos, int duration, bool excludeGripper = false) {
  Serial.print("직접 각도 제어로 이동: Shoulder=");
  Serial.print(pos.shoulderAngle);
  Serial.print(", Elbow=");
  Serial.print(pos.elbowAngle);
  Serial.print(", Wrist=");
  Serial.print(pos.wristAngle);
  Serial.print(", Base=");
  Serial.print(pos.baseAngle);
  Serial.print(", WristRot=");
  Serial.print(pos.wristRotAngle);
  
  if (!excludeGripper) {
    Serial.print(", Gripper=");
    Serial.print(pos.gripperAngle);
  }
  Serial.println();
  
  setAngleMG996(BASE_SERVO, pos.baseAngle);
  setAngleMG996(SHOULDER_SERVO, pos.shoulderAngle);
  setAngleMG996(ELBOW_SERVO, pos.elbowAngle);
  setAngleMG996(WRIST_PITCH_SERVO, pos.wristAngle);
  setAngleMG996(WRIST_ROT_SERVO, pos.wristRotAngle);
  
  if (!excludeGripper) {
    setAngleMG996(GRIPPER_SERVO, pos.gripperAngle);
  }
  
  delay(duration);
}

// ========== 그리퍼 유지 이동 함수 ==========
void moveToDirectPositionWithGrip(DirectPosition pos, int duration) {
  Serial.println("*** 그리퍼 파지 상태 유지하며 이동 ***");
  moveToDirectPosition(pos, duration, true);
}

// ========== 부드러운 이동 함수 ==========
void smoothMoveToDirectPosition(DirectPosition startPos, DirectPosition endPos, int duration, bool excludeGripper = false) {
  int steps = 20;
  int stepDelay = duration / steps;
  
  for (int step = 1; step <= steps; step++) {
    DirectPosition currentPos;
    currentPos.shoulderAngle = startPos.shoulderAngle + ((endPos.shoulderAngle - startPos.shoulderAngle) * step) / steps;
    currentPos.elbowAngle = startPos.elbowAngle + ((endPos.elbowAngle - startPos.elbowAngle) * step) / steps;
    currentPos.wristAngle = startPos.wristAngle + ((endPos.wristAngle - startPos.wristAngle) * step) / steps;
    currentPos.baseAngle = startPos.baseAngle + ((endPos.baseAngle - startPos.baseAngle) * step) / steps;
    currentPos.gripperAngle = startPos.gripperAngle + ((endPos.gripperAngle - startPos.gripperAngle) * step) / steps;
    currentPos.wristRotAngle = startPos.wristRotAngle + ((endPos.wristRotAngle - startPos.wristRotAngle) * step) / steps;
    
    setAngleMG996(BASE_SERVO, currentPos.baseAngle);
    setAngleMG996(SHOULDER_SERVO, currentPos.shoulderAngle);
    setAngleMG996(ELBOW_SERVO, currentPos.elbowAngle);
    setAngleMG996(WRIST_PITCH_SERVO, currentPos.wristAngle);
    setAngleMG996(WRIST_ROT_SERVO, currentPos.wristRotAngle);
    
    if (!excludeGripper) {
      setAngleMG996(GRIPPER_SERVO, currentPos.gripperAngle);
    }
    
    delay(stepDelay);
  }
}

// ========== 메니퓰레이터 책상 청소 루틴 함수 ==========
void runDeskCleaningRoutine() {
  if (g_isTaskRunning) {
    server.send(200, "text/plain", "다른 작업이 이미 실행 중입니다. 완료될 때까지 기다려 주세요.");
    return;
  }
  g_isTaskRunning = true;

  String html = "<!DOCTYPE html><html lang='ko'><head><meta charset='UTF-8'><title>작업 시작</title><meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body{font-family:sans-serif; text-align:center; padding-top: 50px; background-color: #f4f4f9;} div{display:inline-block; background:white; padding: 20px 40px; border-radius:10px; box-shadow: 0 4px 10px rgba(0,0,0,0.1);}</style>";
  html += "</head><body><div><h2>책상 청소 루틴을 시작합니다.</h2><p>2초 후에 메인 페이지로 돌아갑니다.</p></div></body></html>";
  server.send(200, "text/html", html);

  Serial.println("\n=== 6DOF 메니퓰레이터 책상 청소 사이클 시작 ===");
  moveToDirectPosition(homePosition, 3000);
  delay(1000);
  smoothMoveToDirectPosition(homePosition, clothPosition, 4000);
  delay(1000);
  setAngleMG996(GRIPPER_SERVO, 130);
  delay(1500);
  moveToDirectPositionWithGrip(liftPosition, 2000);
  delay(1000);
  moveToDirectPositionWithGrip(deskCleanStart, 3000);
  delay(1000);
  for (int cleanCycle = 1; cleanCycle <= 5; cleanCycle++) {
    for (int baseAngle = 110; baseAngle <= 160; baseAngle += 2) { setAngleMG996(BASE_SERVO, baseAngle); delay(50); }
    delay(200);
    for (int baseAngle = 150; baseAngle >= 120; baseAngle -= 2) { setAngleMG996(BASE_SERVO, baseAngle); delay(50); }
    delay(200);
  }
  moveToDirectPositionWithGrip(liftPosition, 2000);
  delay(1000);
  smoothMoveToDirectPosition(liftPosition, clothPosition, 3000, true);
  delay(1000);
  setAngleMG996(GRIPPER_SERVO, 40);
  delay(1500);
  smoothMoveToDirectPosition(clothPosition, homePosition, 4000);
  delay(2000);
  Serial.println("=== 6DOF 메니퓰레이터 책상 청소 사이클 완료 ===\n");

  g_isTaskRunning = false;
}

// ========== 단일 팔 제어 함수 ==========
// void moveSingleArm() {
//   if (g_isTaskRunning) {
//     server.send(200, "text/plain", "다른 작업이 이미 실행 중입니다. 완료될 때까지 기다려 주세요.");
//     return;
//   }
//   g_isTaskRunning = true;

//   String html = "<!DOCTYPE html><html lang='ko'><head><meta charset='UTF-8'><title>작업 시작</title><meta http-equiv='refresh' content='2;url=/'>";
//   html += "<style>body{font-family:sans-serif; text-align:center; padding-top: 50px; background-color: #f4f4f9;} div{display:inline-block; background:white; padding: 20px 40px; border-radius:10px; box-shadow: 0 4px 10px rgba(0,0,0,0.1);}</style>";
//   html += "</head><body><div><h2>단일 팔 동작을 시작합니다.</h2><p>2초 후에 메인 페이지로 돌아갑니다.</p></div></body></html>";
//   server.send(200, "text/html", html);
  
//   Serial.println("=== 단일 팔 동작 시작 ===");
//   setAngleMG996(SINGLE_ARM_SERVO, 90);
//   delay(500);
//   for (int angle = 90; angle >= 1; angle -= 1) {
//     setAngleMG996(SINGLE_ARM_SERVO, angle);
//     delay(50);
//   }
//   Serial.println("단일 팔 동작 완료 (90도 위치)");
//   Serial.println("=== 단일 팔 동작 완료 ===\n");
//   delay(1000);

//   g_isTaskRunning = false;
// }

void moveSingleArm() {
  Serial.println("=== 단일 팔 동작 시작 ===");
  Serial.println("단일 팔을 90도에서 0도로 이동 중...");
  
  count ++;
   // 0도에서 시작
 if(count%2==1)
{
  setAngleMG996(SINGLE_ARM_SERVO, 90);
  delay(500);
} 
  // 90도로 부드럽게 이동
   if(count%2==0)
{
  for (int angle = 90; angle >= 1; angle -= 1) {
    setAngleMG996(SINGLE_ARM_SERVO, angle);
    delay(50);
  }
}
  Serial.println("단일 팔 동작 완료 (90도 위치)");
  Serial.println("=== 단일 팔 동작 완료 ===\n");
  delay(1000);
}

// ========== SG90 3개 순차 제어 함수 ==========
void controlSG90Sequence() {
  if (g_isTaskRunning) {
    server.send(200, "text/plain", "다른 작업이 이미 실행 중입니다. 완료될 때까지 기다려 주세요.");
    return;
  }
  g_isTaskRunning = true;
  
  String html = "<!DOCTYPE html><html lang='ko'><head><meta charset='UTF-8'><title>작업 시작</title><meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body{font-family:sans-serif; text-align:center; padding-top: 50px; background-color: #f4f4f9;} div{display:inline-block; background:white; padding: 20px 40px; border-radius:10px; box-shadow: 0 4px 10px rgba(0,0,0,0.1);}</style>";
  html += "</head><body><div><h2>약 배출을 시작합니다.</h2><p>2초 후에 메인 페이지로 돌아갑니다.</p></div></body></html>";
  server.send(200, "text/html", html);

  Serial.println("=== SG90 순차 제어 시작 ===");
  sg90CallCount++;
  int targetServo;
  if (sg90CallCount > 3) sg90CallCount = 1;

  if (sg90CallCount == 1) { targetServo = SG90_SERVO_1; } 
  else if (sg90CallCount == 2) { targetServo = SG90_SERVO_2; }
  else { targetServo = SG90_SERVO_3; }
  
  Serial.printf("SG90 채널 %d 을(를) 0도에서 90도로 이동 중...\n", targetServo);
  setAngleSG90(targetServo, 0);
  delay(500);
  for (int angle = 0; angle <= 90; angle += 3) {
    setAngleSG90(targetServo, angle);
    delay(30);
  }
  Serial.printf("SG90 채널 %d 동작 완료 (90도 위치)\n", targetServo);
  Serial.println("=== SG90 순차 제어 완료 ===\n");
  delay(1000);

  g_isTaskRunning = false;
}

// =================================================================
// 웹페이지 UI 및 핸들러 함수
// =================================================================

// <<< 추가된 함수: 현재 BPM 값을 보여주는 페이지를 처리 >>>
void handleGetBPM() {
  String html = "<!DOCTYPE html><html lang='ko'><head><meta charset='UTF-8'><title>심박수 확인</title><meta http-equiv='refresh' content='4;url=/'>";
  html += "<style>body{font-family:sans-serif; text-align:center; padding-top: 50px; background-color: #f4f4f9;} div{display:inline-block; background:white; padding: 20px 40px; border-radius:10px; box-shadow: 0 4px 10px rgba(0,0,0,0.1);}</style>";
  html += "</head><body><div>";
  html += "<h2>현재 심박수(BPM)</h2>";
  if (currentBPM > 0) {
    html += "<p style='font-size: 2.5em; color: #e74c3c; margin: 10px 0;'>" + String(currentBPM) + "</p>";
  } else {
    html += "<p>측정된 심박수 값이 없습니다.<br>센서에 손가락을 올바르게 올려주세요.</p>";
  }
  html += "<p style='font-size: 0.9em; color: #777;'>4초 후에 메인 페이지로 돌아갑니다.</p>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
  Serial.println("Sent current BPM value to client: " + String(currentBPM));
}

void handleRoot() {
  String html = "<!DOCTYPE html><html lang='ko'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>ESP32 로봇팔 제어</title>";
  html += "<style>";
  html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif; background-color: #f4f4f9; color: #333; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; }";
  html += ".container { background: white; padding: 25px 30px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); width: 90%; max-width: 500px; text-align: center; }";
  html += "h1 { color: #2c3e50; margin-bottom: 25px; }";
  html += "p { margin: 15px 0; }";
  html += "a { text-decoration: none; }";
  html += "button { background-color: #3498db; color: white; padding: 15px 20px; border: none; border-radius: 8px; cursor: pointer; font-size: 1.1em; width: 100%; transition: background-color 0.3s, transform 0.2s; }";
  html += "button:hover { background-color: #2980b9; transform: translateY(-2px); }";
  html += "button:active { transform: translateY(0); }";
  // <<< 수정: BPM 버튼을 위한 스타일 추가 >>>
  html += "button.bpm-btn { background-color: #2ecc71; }";
  html += "button.bpm-btn:hover { background-color: #27ae60; }";
  html += ".info { margin-top: 25px; padding-top: 15px; border-top: 1px solid #eee; font-size: 0.9em; color: #555; }";
  html += "</style>";
  html += "</head><body><div class='container'>";
  html += "<h1>ESP32 로봇팔 제어</h1>";
  html += "<p><a href=\"/manipulator_work_cleaning\"><button>책상 청소 시작</button></a></p>";
  html += "<p><a href=\"/single_arm_cleaning\"><button>단일 팔 동작</button></a></p>";
  html += "<p><a href=\"/get_medicine\"><button>약 배출하기</button></a></p>";
  // <<< 수정: BPM 확인 버튼 추가 >>>
  html += "<p><a href=\"/get_bpm\"><button class='bpm-btn'>현재 심박수 확인</button></a></p>";
  html += "<div class='info'>";
  // <<< 수정: 안내 문구 변경 >>>
  html += "<p>심박수(BPM)는 위 녹색 버튼을 눌러 확인해 주세요.</p>";
  html += "</div>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

// ... 기타 핸들러 함수들 ...
void handleLedOn() {
  digitalWrite(LED_BUILTIN, HIGH);
  server.send(200, "text/plain", "LED is ON");
  Serial.println("LED turned ON");
}

void handleLedOff() {
  digitalWrite(LED_BUILTIN, LOW);
  server.send(200, "text/plain", "LED is OFF");
  Serial.println("LED turned OFF");
}

void handleSetServo() {
  String angleArg = server.arg("angle");
  if (angleArg == "") {
    server.send(400, "text/plain", "Angle parameter missing");
    return;
  }
  int newAngle = angleArg.toInt();
  if (newAngle < 0 || newAngle > 180) {
    server.send(400, "text/plain", "Angle out of range (0-180)");
    return;
  }
  myservo.write(newAngle);
  currentServoAngle = newAngle;
  delay(15);
  String responseMessage = "Servo angle set to " + String(newAngle);
  server.send(200, "text/plain", responseMessage);
  Serial.println(responseMessage);
}

void handleGetServoAngle() {
    String jsonResponse = "{\"angle\":" + String(currentServoAngle) + "}";
    server.send(200, "application/json", jsonResponse);
    Serial.println("Sent current servo angle: " + String(currentServoAngle));
}

void handleEspInfo() {
  String deviceName = "CareLight_ESP32_" + WiFi.macAddress().substring(9);
  deviceName.replace(":", "");
  String status = "Online";
  String currentSsid = WiFi.SSID();

  String jsonResponse = "{";
  jsonResponse += "\"name\":\"" + deviceName + "\",";
  jsonResponse += "\"status\":\"" + status + "\",";
  jsonResponse += "\"wifi_ssid\":\"" + currentSsid + "\",";
  jsonResponse += "\"servo_angle\":" + String(currentServoAngle) + ",";
  jsonResponse += "\"bpm\":" + String(currentBPM);
  jsonResponse += "}";

  server.send(200, "application/json", jsonResponse);
  Serial.println("Sent ESP32 info (with servo and BPM) to client.");
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Wire.begin(8, 9);
  
  pwm.begin();
  pwm.setPWMFreq(50);
  delay(1000);

  moveToDirectPosition(homePosition, 2000);
  setAngleMG996(SINGLE_ARM_SERVO, 0);
  setAngleSG90(SG90_SERVO_1, 0);
  setAngleSG90(SG90_SERVO_2, 0);
  setAngleSG90(SG90_SERVO_3, 0);
  
  myservo.attach(SERVO_PIN);
  myservo.write(currentServoAngle);
  Serial.println("Servo attached and set to initial position.");

  pulseSensor.analogInput(PULSE_SENSOR_PIN);
  pulseSensor.setThreshold(PULSE_SENSOR_THRESHOLD);
  if (pulseSensor.begin()) {
    Serial.println("PulseSensor Playground object created!");
  } else {
    Serial.println("PulseSensor Playground object NOT created!");
  }

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, passwd);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());

    // 웹 서버 핸들러 등록
    server.on("/", HTTP_GET, handleRoot);
    server.on("/ledon", HTTP_GET, handleLedOn);
    server.on("/ledoff", HTTP_GET, handleLedOff);
    server.on("/info", HTTP_GET, handleEspInfo);
    server.on("/set_servo", HTTP_GET, handleSetServo);
    server.on("/get_servo_angle", HTTP_GET, handleGetServoAngle);
    server.on("/manipulator_work_cleaning", HTTP_GET, runDeskCleaningRoutine);
    server.on("/single_arm_cleaning", HTTP_GET, moveSingleArm);
    server.on("/get_medicine", HTTP_GET, controlSG90Sequence);
    // <<< 추가: BPM 확인 핸들러 등록 >>>
    server.on("/get_bpm", HTTP_GET, handleGetBPM);
  
    server.begin();
    Serial.println("HTTP server started");
  } else {
    Serial.println("\nFailed to connect to WiFi.");
  } 
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  if (pulseSensor.sawStartOfBeat()) { 
    currentBPM = pulseSensor.getBeatsPerMinute();
    if (currentBPM > 0) {
        Serial.print("BPM: ");
        Serial.println(currentBPM);
    }
  }

  delay(20);
}
