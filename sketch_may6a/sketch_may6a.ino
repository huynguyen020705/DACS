/*
 * ĐỒ ÁN: HỆ THỐNG PHÁT HIỆN CHÁY & CẢNH BÁO QUA BLYNK
 * Cập nhật: Fix lỗi Servo không tự đóng (Dùng tư duy Target State)
 */

#define BLYNK_TEMPLATE_ID "TMPL6HJOM9NWc"
#define BLYNK_TEMPLATE_NAME "Fire Alarm System"
#define BLYNK_AUTH_TOKEN "bxX6zpwfOQohgvU1xPvkabb7BoOLU1Ib"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <ESP32Servo.h>

// --- KHAI BÁO CHÂN (PINOUT) ---
#define MQ2_PIN       34  
#define FLAME_PIN     35  // Cắm chân D0
#define PIR_PIN       27  
#define DHT_PIN       32  
#define SERVO_PIN     18  // Servo MG90S 360
#define RELAY_PIN     26  // Relay điều khiển Bơm
#define BUZZER_PIN    25  
#define LED_PIN       14  
#define BTN_MUTE_PIN  33  

// --- CẤU HÌNH WIFI ---
char ssid[] = "Huy Nguyễn";
char pass[] = "bolabocuacaccon";

// --- KHỞI TẠO ĐỐI TƯỢNG ---
#define DHTTYPE DHT11     
DHT dht(DHT_PIN, DHTTYPE);
Servo windowServo;
BlynkTimer timer;

// --- BIẾN HỆ THỐNG & FSM ---
enum SystemState { NORMAL, ALARM_FIRE, ALARM_GAS, WARN_COOKING, PANIC };
SystemState currentState = NORMAL;

float temp = 0.0, hum = 0.0, lastTemp = 0.0;
int gasValue = 0;
int flameValue = HIGH; 
bool pirState = false, isMuted = false;

// Biến tính toán tốc độ tăng nhiệt
unsigned long lastTempCheckTime = 0;
const float FIRE_TEMP_RATE = 5.0; 

// --- BIẾN ĐIỀU KHIỂN SERVO ---
bool isWindowOpen = false;
bool isWindowOpening = false;
bool isWindowClosing = false;
bool windowShouldBeOpen = false; // BIẾN MỚI: Theo dõi mục tiêu đóng/mở cửa
unsigned long windowMoveStartTime = 0;

// --- HÀM KHỞI TẠO ---
void setup() {
  Serial.begin(115200);
  
  pinMode(FLAME_PIN, INPUT); 
  pinMode(PIR_PIN, INPUT);
  pinMode(BTN_MUTE_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); 
  
  windowServo.attach(SERVO_PIN);
  windowServo.write(90); 
  
  dht.begin();
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  
  timer.setInterval(2000L, readSensors);    
  timer.setInterval(1000L, evaluateLogic);  
  timer.setInterval(100L, checkMuteButton); 
  timer.setInterval(50L, handleActuators);  
  
  Serial.println("He thong da khoi dong xong!");
}

void loop() {
  Blynk.run();
  timer.run();
}

// --- 1. Đọc dữ liệu cảm biến ---
void readSensors() {
  temp = dht.readTemperature();
  hum = dht.readHumidity();
  gasValue = analogRead(MQ2_PIN);
  flameValue = digitalRead(FLAME_PIN); 
  pirState = digitalRead(PIR_PIN);

  Serial.print("Nhiet do: "); Serial.print(temp);
  Serial.print(" | Gas: "); Serial.print(gasValue);
  Serial.print(" | Lua: "); Serial.println(flameValue);

  Blynk.virtualWrite(V0, temp);
  Blynk.virtualWrite(V1, hum);
  Blynk.virtualWrite(V2, gasValue);
  
  String stateStr = "BÌNH THƯỜNG";
  if(currentState == ALARM_FIRE) stateStr = "CHÁY KHẨN CẤP!";
  else if(currentState == ALARM_GAS) stateStr = "RÒ RỈ GAS!";
  else if(currentState == WARN_COOKING) stateStr = "CÓ KHÓI BẾP";
  Blynk.virtualWrite(V3, stateStr);
}

// --- 2. Logic Máy trạng thái ---
void evaluateLogic() {
  float tempRate = 0;
  if (millis() - lastTempCheckTime >= 10000) {
    tempRate = temp - lastTemp;
    lastTemp = temp;
    lastTempCheckTime = millis();
  }

  // LOGIC 1: CHÁY THẬT 
  if (flameValue == LOW || (temp > 55.0 && gasValue > 1500) || tempRate >= FIRE_TEMP_RATE) {
    if(currentState != ALARM_FIRE) {
      currentState = ALARM_FIRE;
      Blynk.logEvent("fire_alert", "PHÁT HIỆN CHÁY! Bơm đang bật!");
    }
    windowShouldBeOpen = true; // Chốt hạ: Phải mở cửa!
  }
  // LOGIC 2: RÒ RỈ GAS 
  else if (gasValue > 1500 && temp < 40.0) {
    if(currentState != ALARM_GAS && currentState != ALARM_FIRE) {
      currentState = ALARM_GAS;
      Blynk.logEvent("gas_alert", "RÒ RỈ GAS! Đã mở thông gió.");
    }
    windowShouldBeOpen = true; // Chốt hạ: Phải mở cửa!
  }
  // LOGIC 3: NẤU ĂN 
  else if (gasValue > 800 && tempRate < FIRE_TEMP_RATE && pirState == HIGH) {
    if(currentState == NORMAL) {
      currentState = WARN_COOKING;
      Blynk.logEvent("smoke_alert", "Phát hiện khói bếp.");
    }
  }
  // TRỞ VỀ BÌNH THƯỜNG
  else if (gasValue < 800 && flameValue == HIGH) { 
    if(currentState != NORMAL) {
      currentState = NORMAL;
      isMuted = false; 
    }
    windowShouldBeOpen = false; // Chốt hạ: An toàn rồi, phải đóng cửa!
  }
}

// --- 3. Xử lý phần cứng đầu ra ---
void handleActuators() {
  
  // 3.1. QUẢN LÝ SERVO THÔNG MINH (Bám sát biến windowShouldBeOpen)
  if (windowShouldBeOpen) {
    if (!isWindowOpen && !isWindowOpening) {
      Serial.println("Bat dau MO cua so...");
      isWindowOpening = true;
      isWindowClosing = false;
      windowMoveStartTime = millis(); 
      windowServo.write(0);           // Quay mở
    }
    else if (isWindowOpening && (millis() - windowMoveStartTime >= 2000)) {
      windowServo.write(90);          // Dừng
      isWindowOpening = false;
      isWindowOpen = true;
      Serial.println("Cua so DA MO xong!");
    }
  } 
  else { // Nếu trạng thái mong muốn là windowShouldBeOpen == false
    // Nếu cửa đang mở, HOẶC đang mở dở dang -> Lập tức quay ngược lại để đóng
    if ((isWindowOpen || isWindowOpening) && !isWindowClosing) {
      Serial.println("Bat dau DONG cua so...");
      isWindowClosing = true;
      isWindowOpening = false;
      windowMoveStartTime = millis(); 
      windowServo.write(180);         // Quay đóng
    }
    else if (isWindowClosing && (millis() - windowMoveStartTime >= 2000)) {
      windowServo.write(90);          // Dừng
      isWindowClosing = false;
      isWindowOpen = false;
      Serial.println("Cua so DA DONG xong!");
    }
  }

  // 3.2. QUẢN LÝ BƠM, CÒI, ĐÈN
  if (currentState == ALARM_FIRE) {
    digitalWrite(RELAY_PIN, LOW);  // Bật máy bơm NGAY LẬP TỨC
    if (!isMuted) {
      digitalWrite(BUZZER_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  } 
  else if (currentState == ALARM_GAS) {
    digitalWrite(RELAY_PIN, HIGH); // Tắt bơm chống nổ
    if (!isMuted) {
      digitalWrite(BUZZER_PIN, (millis() % 1000 < 500) ? HIGH : LOW); 
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
  else {
    digitalWrite(RELAY_PIN, HIGH); // Bình thường tắt hết
    digitalWrite(BUZZER_PIN, LOW); 
    digitalWrite(LED_PIN, LOW);    
  }
}

// --- 4. Nút nhấn tắt còi ---
void checkMuteButton() {
  if (digitalRead(BTN_MUTE_PIN) == LOW) {
    isMuted = true;
  }
}

// BLYNK: Nút nhấn bơm thủ công
BLYNK_WRITE(V4) {
  int manualPump = param.asInt();
  if (manualPump == 1 && currentState != ALARM_GAS) {
    digitalWrite(RELAY_PIN, LOW);
  } else {
    digitalWrite(RELAY_PIN, HIGH);
  }
}