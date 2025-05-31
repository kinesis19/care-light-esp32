#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// Wi-Fi 설정
const char* ssid = "융합씽킹랩 2"; // 사용하는 Wi-Fi SSID로 변경
// const char* password = ""; // 개방형 네트워크의 경우 비밀번호 필요 없음

// LED 설정 (기존 코드 유지)
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

// 서보 모터 설정
Servo myservo;
int servoPin = 17;
int currentServoAngle = 90; // 서보 모터 초기값: 90도

WebServer server(80); // HTTP 서버 포트 80


void handleRoot() {
  String html = "<h1>ESP32 Web Server</h1>";
  html += "<p><a href=\"/ledon\"><button>Turn LED ON</button></a></p>";
  html += "<p><a href=\"/ledoff\"><button>Turn LED OFF</button></a></p>";
  html += "<p>Current Servo Angle: " + String(currentServoAngle) + "</p>";
  html += "<p><form action='/set_servo' method='get'>Angle: <input type='number' name='angle' min='0' max='180' value='" + String(currentServoAngle) + "'> <input type='submit' value='Set Servo'></form></p>";
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

void handleEspInfo() {
  String deviceName = "CareLight_ESP32_" + WiFi.macAddress().substring(9);
  deviceName.replace(":", "");
  String status = "Online";
  String currentSsid = WiFi.SSID();

  String jsonResponse = "{";
  jsonResponse += "\"name\":\"" + deviceName + "\",";
  jsonResponse += "\"status\":\"" + status + "\",";
  jsonResponse += "\"wifi_ssid\":\"" + currentSsid + "\",";
  jsonResponse += "\"servo_angle\":" + String(currentServoAngle);
  jsonResponse += "}";

  server.send(200, "application/json", jsonResponse);
  Serial.println("Sent ESP32 info (with servo angle) to client.");
}

// 서보 모터 각도 설정 핸들러
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

  myservo.write(newAngle); // 서보 모터 각도 설정
  currentServoAngle = newAngle; // 현재 각도 업데이트
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


void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  myservo.attach(servoPin); // 서보 핀 연결
  myservo.write(currentServoAngle); // 초기 각도로 설정
  Serial.println("Servo attached and set to initial position.");

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid); // 개방형 네트워크 연결

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

    server.on("/", HTTP_GET, handleRoot); // 루트 경로 핸들러
    server.on("/ledon", HTTP_GET, handleLedOn); // LED ON 핸들러
    server.on("/ledoff", HTTP_GET, handleLedOff); // LED OFF 핸들러
    server.on("/info", HTTP_GET, handleEspInfo); // ESP32 정보 제공 핸들러 (서보 각도 포함)
    server.on("/set_servo", HTTP_GET, handleSetServo); // 서보 각도 설정 핸들러
    server.on("/get_servo_angle", HTTP_GET, handleGetServoAngle); // 서보 각도만 가져오는 핸들러

    server.begin(); // 웹 서버 시작
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
    server.handleClient(); // 클라이언트 요청 처리
  }
}