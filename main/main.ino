#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#define USE_ARDUINO_INTERRUPTS true
#include <PulseSensorPlayground.h>

// Wi-Fi 설정
const char* ssid = "Akashic";
const char* passwd = "microsoft";
// const char* ssid = "iptime";

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

// Pulse Sensor 설정
const int PULSE_SENSOR_PIN = 5;
const int PULSE_SENSOR_THRESHOLD = 550;
PulseSensorPlayground pulseSensor;
int currentBPM = 0;

WebServer server(80);

/ ========== 직접 각도 제어용 포지션 구조체 ==========
struct DirectPosition {
  int baseAngle;        // 서보 0
  int shoulderAngle;    // 서보 1
  int elbowAngle;       // 서보 2
  int wristRotAngle;    // 서보 3
  int wristAngle;       // 서보 4 (피치)
  int gripperAngle;     // 서보 5
};

// ======== 배열 순서 기준으로 값 재정렬 ========
DirectPosition homePosition       = {90, 75, 90, 90, 90, 90};
DirectPosition clothPosition      = {120, 75, 140, 130, 170, 40};
DirectPosition liftPosition       = {120, 65, 120, 110, 155, 130};
DirectPosition deskCleanStart     = {100, 75, 140, 145, 170, 130};
DirectPosition deskCleanEnd       = {20,  75, 140, 145, 180, 130};  


// ========== SG90 제어 관련 변수 ==========
int sg90CallCount = 0; // SG90 함수 호출 횟수 카운터

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
  
  // 서보 모터 제어
  setAngleMG996(BASE_SERVO, pos.baseAngle);
  setAngleMG996(SHOULDER_SERVO, pos.shoulderAngle);
  setAngleMG996(ELBOW_SERVO, pos.elbowAngle);
  setAngleMG996(WRIST_PITCH_SERVO, pos.wristAngle);
  setAngleMG996(WRIST_ROT_SERVO, pos.wristRotAngle);  // 구조체의 값 사용
  
  // 그리퍼 제어 (excludeGripper가 false일 때만)
  if (!excludeGripper) {
    setAngleMG996(GRIPPER_SERVO, pos.gripperAngle);
  }
  
  delay(duration);
}

// 그리퍼를 잡은 상태로 위치 이동하는 함수
void moveToDirectPositionWithGrip(DirectPosition pos, int duration) {
  Serial.println("*** 그리퍼 파지 상태 유지하며 이동 ***");
  moveToDirectPosition(pos, duration, true); // 그리퍼 제외하고 이동
}

// ========== 부드러운 이동 함수 ==========
void smoothMoveToDirectPosition(DirectPosition startPos, DirectPosition endPos, int duration, bool excludeGripper = false) {
  int steps = 20; // 이동을 20단계로 나누어 부드럽게 이동
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
  Serial.println("\n=== 6DOF 메니퓰레이터 책상 청소 사이클 시작 ===");
  
  // 1. 초기 위치로 이동
  Serial.println("1. 홈 위치로 이동 중...");
  moveToDirectPosition(homePosition, 3000);
  delay(1000);
  
  // 2. 책상 위 걸레 위치로 부드럽게 이동 (그리퍼 열린 상태)
  Serial.println("2. 책상 위 걸레 위치로 부드러운 이동 중...");
  smoothMoveToDirectPosition(homePosition, clothPosition, 4000);
  delay(1000);
  
  // 3. 그리퍼로 걸레 잡기
  Serial.println("3. 그리퍼로 걸레 잡는 중...");
  setAngleMG996(GRIPPER_SERVO, 130);
  delay(1500);
  
  // 4. 걸레를 집은 후 살짝 올라가기
  Serial.println("4. 걸레를 집은 후 살짝 올라가는 중...");
  moveToDirectPositionWithGrip(liftPosition, 2000);
  delay(1000);
  
  // 5. 책상 청소 시작 위치로 이동
  Serial.println("5. 책상 청소 시작 위치로 이동 중...");
  moveToDirectPositionWithGrip(deskCleanStart, 3000);
  delay(1000);
  
  // 6. 5번 왔다갔다하며 책상 청소
  Serial.println("6. 책상 청소 동작 시작 (5번 왔다갔다)");
  
  for (int cleanCycle = 1; cleanCycle <= 5; cleanCycle++) {
    // 왼쪽에서 오른쪽으로 이동
    Serial.print("책상 청소 ");
    Serial.print(cleanCycle);
    Serial.println("회차 - 왼쪽에서 오른쪽으로");
    
    for (int baseAngle = 110; baseAngle <= 160; baseAngle += 2) {
      setAngleMG996(BASE_SERVO, baseAngle);
      delay(50);
    }
    delay(200);
    
    // 오른쪽에서 왼쪽으로 이동
    Serial.print("책상 청소 ");
    Serial.print(cleanCycle);
    Serial.println("회차 - 오른쪽에서 왼쪽으로");
    
    for (int baseAngle = 150; baseAngle >= 120; baseAngle -= 2) {
      setAngleMG996(BASE_SERVO, baseAngle);
      delay(50);
    }
    delay(200);
  }
  
  // 7. 청소 완료 후 다시 살짝 올라가기
  Serial.println("7. 청소 완료 - 살짝 올라가는 중...");
  moveToDirectPositionWithGrip(liftPosition, 2000);
  delay(1000);
  
  // 8. 걸레를 원래 자리로 가져가기
  Serial.println("8. 걸레를 원래 자리로 가져가는 중...");
  smoothMoveToDirectPosition(liftPosition, clothPosition, 3000, true);
  delay(1000);
  
  // 9. 걸레 놓기 (그리퍼 열기)
  Serial.println("9. 걸레를 책상에 놓는 중...");
  setAngleMG996(GRIPPER_SERVO, 40);
  delay(1500);
  
  // 10. 홈 위치로 최종 복귀
  Serial.println("10. 책상 청소 완료 - 홈 위치로 최종 복귀");
  smoothMoveToDirectPosition(clothPosition, homePosition, 4000);
  delay(2000);
  
  Serial.println("=== 6DOF 메니퓰레이터 책상 청소 사이클 완료 ===\n");
}

// ========== 단일 팔 제어 함수 ==========
void moveSingleArm() {
  Serial.println("=== 단일 팔 동작 시작 ===");
  Serial.println("단일 팔을 90도에서 0도로 이동 중...");
  
  // 0도에서 시작
  setAngleMG996(SINGLE_ARM_SERVO, 90);
  delay(500);
  
  // 90도로 부드럽게 이동
  for (int angle = 90; angle >= 1; angle -= 1) {
    setAngleMG996(SINGLE_ARM_SERVO, angle);
    delay(50);
  }
  
  Serial.println("단일 팔 동작 완료 (90도 위치)");
  Serial.println("=== 단일 팔 동작 완료 ===\n");
  delay(1000);
}

// ========== SG90 3개 순차 제어 함수 ==========
// 함수 호출 전에는 닫혀있는거임
void controlSG90Sequence() {
  Serial.println("=== SG90 순차 제어 시작 ===");
  
  sg90CallCount++; // 호출 횟수 증가
  
  int targetServo;
  if (sg90CallCount == 1) {
    targetServo = SG90_SERVO_1;
    Serial.println("첫 번째 SG90 모터 동작 (채널 7)");
  } else if (sg90CallCount == 2) {
    targetServo = SG90_SERVO_2;
    Serial.println("두 번째 SG90 모터 동작 (채널 8)");
  } else if (sg90CallCount == 3) {
    targetServo = SG90_SERVO_3;
    Serial.println("세 번째 SG90 모터 동작 (채널 9)");
    sg90CallCount = 0; // 카운터 리셋
  } else {
    // 안전장치
    sg90CallCount = 1;
    targetServo = SG90_SERVO_1;
    Serial.println("첫 번째 SG90 모터 동작 (채널 7) - 리셋됨");
  }
  
  // 선택된 서보를 0도에서 90도로 이동
  Serial.print("SG90 채널 ");
  Serial.print(targetServo);
  Serial.println("을 0도에서 90도로 이동 중...");
  
  // 0도에서 시작
  setAngleSG90(targetServo, 0);
  delay(500);
  
  // 90도로 부드럽게 이동
  for (int angle = 0; angle <= 90; angle += 3) {
    setAngleSG90(targetServo, angle);
    delay(30);
  }
  
  Serial.print("SG90 채널 ");
  Serial.print(targetServo);
  Serial.println(" 동작 완료 (90도 위치)");
  Serial.println("=== SG90 순차 제어 완료 ===\n");
  delay(1000);
}


void handleRoot() {
  String html = "<h1>ESP32 Web Server</h1>";
  html += "<p><a href=\"/ledon\"><button>Turn LED ON</button></a></p>";
  html += "<p><a href=\"/ledoff\"><button>Turn LED OFF</button></a></p>";
  html += "<p>Current Servo Angle: " + String(currentServoAngle) + "</p>";
  html += "<p><form action='/set_servo' method='get'>Angle: <input type='number' name='angle' min='0' max='180' value='" + String(currentServoAngle) + "'> <input type='submit' value='Set Servo'></form></p>";
  html += "<p>Current BPM: " + String(currentBPM) + "</p>";
  server.send(200, "text/html", html);
}

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
    Serial.println("Set servo failed: Angle parameter missing");
    return;
  }
  int newAngle = angleArg.toInt();
  if (newAngle < 0 || newAngle > 180) {
    server.send(400, "text/plain", "Angle out of range (0-180)");
    Serial.println("Set servo failed: Angle out of range");
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

 // I2C 핀 설정 (ESP32-S3 LOLIN 기본값)
  Wire.begin(8, 9); // SDA=8, SCL=9
  
  pwm.begin();
  pwm.setPWMFreq(50);  // 서보는 50Hz 주파수를 사용함
  delay(1000);

  // 메니퓰레이터 초기 위치
  moveToDirectPosition(homePosition, 2000);
  
  // 단일 팔 초기 위치 (0도)
  setAngleMG996(SINGLE_ARM_SERVO, 0);
  
  // SG90 모터들 초기 위치 (0도)
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

  // WiFi.begin(ssid); // public network일 때
  WiFi.begin(ssid, passwd); // private network일 때

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();
  Serial.println("DEBUG: Wi-Fi connection attempt loop finished.");
  Serial.print("DEBUG: Current WiFi.status() is: ");
  Serial.println(WiFi.status());

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("DEBUG: Entered 'IF WL_CONNECTED' block.");
    Serial.println("");
    Serial.println("WiFi connected!");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/ledon", HTTP_GET, handleLedOn);
    server.on("/ledoff", HTTP_GET, handleLedOff);
    server.on("/info", HTTP_GET, handleEspInfo);
    server.on("/set_servo", HTTP_GET, handleSetServo);
    server.on("/get_servo_angle", HTTP_GET, handleGetServoAngle);
    server.on("/manipulator_work_cleaning", HTTP_GET, runDeskCleaningRoutine);
    server.on("/single_arm_cleaning", HTTP_GET, moveSingleArm);
    server.on("/get_medicine", HTTP_GET, controlSG90Sequence);
  
    


    server.begin();
    Serial.println("HTTP server started");
  } else {
    Serial.println("DEBUG: Entered 'ELSE (not WL_CONNECTED)' block.");
    Serial.println("");
    Serial.println("Failed to connect to WiFi. Please check credentials or signal.");
    Serial.print("Final WiFi status code: ");
    Serial.println(WiFi.status());
  }
  Serial.println("DEBUG: End of setup() WiFi check section.");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient(); // 웹 서버 클라이언트 요청 처리
  }

  if (pulseSensor.sawStartOfBeat()) { 
    currentBPM = pulseSensor.getBeatsPerMinute();
    // BPM 값이 유효할 때만
    if (currentBPM > 0) {
        Serial.print("BPM: ");
        Serial.println(currentBPM);
    }
  }

  delay(20);
}#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#define USE_ARDUINO_INTERRUPTS true
#include <PulseSensorPlayground.h>

// Wi-Fi 설정
const char* ssid = "Akashic";
const char* passwd = "microsoft";
// const char* ssid = "iptime";

// LED 설정
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

// 서보 모터 설정
Servo myservo;
const int SERVO_PIN = 17;
int currentServoAngle = 90;

// Pulse Sensor 설정
const int PULSE_SENSOR_PIN = 4;
const int PULSE_SENSOR_THRESHOLD = 550;
PulseSensorPlayground pulseSensor;
int currentBPM = 0;

WebServer server(80);

void handleRoot() {
  String html = "<h1>ESP32 Web Server</h1>";
  html += "<p><a href=\"/ledon\"><button>Turn LED ON</button></a></p>";
  html += "<p><a href=\"/ledoff\"><button>Turn LED OFF</button></a></p>";
  html += "<p>Current Servo Angle: " + String(currentServoAngle) + "</p>";
  html += "<p><form action='/set_servo' method='get'>Angle: <input type='number' name='angle' min='0' max='180' value='" + String(currentServoAngle) + "'> <input type='submit' value='Set Servo'></form></p>";
  html += "<p>Current BPM: " + String(currentBPM) + "</p>";
  server.send(200, "text/html", html);
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

  // WiFi.begin(ssid); // public network일 때
  WiFi.begin(ssid, passwd); // private network일 때

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();
  Serial.println("DEBUG: Wi-Fi connection attempt loop finished.");
  Serial.print("DEBUG: Current WiFi.status() is: ");
  Serial.println(WiFi.status());

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("DEBUG: Entered 'IF WL_CONNECTED' block.");
    Serial.println("");
    Serial.println("WiFi connected!");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/ledon", HTTP_GET, handleLedOn);
    server.on("/ledoff", HTTP_GET, handleLedOff);
    server.on("/info", HTTP_GET, handleEspInfo);
    server.on("/set_servo", HTTP_GET, handleSetServo);
    server.on("/get_servo_angle", HTTP_GET, handleGetServoAngle);

    server.begin();
    Serial.println("HTTP server started");
  } else {
    Serial.println("DEBUG: Entered 'ELSE (not WL_CONNECTED)' block.");
    Serial.println("");
    Serial.println("Failed to connect to WiFi. Please check credentials or signal.");
    Serial.print("Final WiFi status code: ");
    Serial.println(WiFi.status());
  }
  Serial.println("DEBUG: End of setup() WiFi check section.");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient(); // 웹 서버 클라이언트 요청 처리
  }

  if (pulseSensor.sawStartOfBeat()) { 
    currentBPM = pulseSensor.getBeatsPerMinute();
    // BPM 값이 유효할 때만
    if (currentBPM > 0) {
        Serial.print("BPM: ");
        Serial.println(currentBPM);
    }
  }

  delay(20);
}
