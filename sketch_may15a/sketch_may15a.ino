/*
 * ĐỒ ÁN: HỆ THỐNG PHÁT HIỆN CHÁY & CẢNH BÁO QUA BLYNK
 * Cập nhật: Thêm hiển thị Serial Monitor để Debug, Giữ nguyên FSM & Sync Blynk
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
#define FLAME_PIN     35  
#define PIR_PIN       27  
#define DHT_PIN       32  
#define SERVO_PIN     18  
#define RELAY_PIN     26  
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

// --- BIẾN ĐIỀU KHIỂN ---
bool isWindowOpen = false;
bool isWindowOpening = false;
bool isWindowClosing = false;
bool windowShouldBeOpen = false; 
unsigned long windowMoveStartTime = 0;

// BIẾN QUẢN LÝ BƠM (Dùng chung cho cả Tự động & Thủ công)
bool manualPumpState = false; 

// --- HÀM KHỞI TẠO ---
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- DANG KHOI DONG HE THONG ---");
  
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
  
  Serial.print("Dang ket noi WiFi va Blynk...");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  Serial.println(" KET NOI THANH CONG!");
  
  timer.setInterval(2000L, readSensors);    
  timer.setInterval(1000L, evaluateLogic);  
  timer.setInterval(100L, checkMuteButton); 
  timer.setInterval(50L, handleActuators);  
  
  // Khi vừa khởi động, ép nút trên app phải tắt
  Blynk.virtualWrite(V4, 0); 
  
  Serial.println(">>> HE THONG DA SAN SANG HOAT DONG! <<<");
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

  // [IN RA SERIAL MONITOR]
  Serial.print("[Sensor] Nhiet: "); Serial.print(temp);
  Serial.print("*C | Do am: "); Serial.print(hum);
  Serial.print("% | Gas: "); Serial.print(gasValue);
  Serial.print(" | Lua(0=Co,1=Khong): "); Serial.print(flameValue);
  Serial.print(" | PIR: "); Serial.println(pirState);

  Blynk.virtualWrite(V0, temp);
  Blynk.virtualWrite(V1, hum);
  Blynk.virtualWrite(V2, gasValue);
  
  String stateStr = "BÌNH THƯỜNG";
  if(currentState == ALARM_FIRE) stateStr = "CHÁY KHẨN CẤP!";
  else if(currentState == ALARM_GAS) stateStr = "RÒ RỈ GAS!";
  else if(currentState == WARN_COOKING) stateStr = "CÓ KHÓI BẾP";
  Blynk.virtualWrite(V3, stateStr);
}

// --- 2. Logic Máy trạng thái (CÓ ĐỒNG BỘ NÚT BẤM APP) ---
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
      Serial.println("\n[FSM] ===> PHAT HIEN CHAY KHAN CAP! Tu dong bat bom! <===");
      Blynk.logEvent("fire_alert", "PHÁT HIỆN CHÁY! Bơm đang tự động bật!");
      
      manualPumpState = true;
      Blynk.virtualWrite(V4, 1); 
    }
    windowShouldBeOpen = true; 
  }
  // LOGIC 2: RÒ RỈ GAS 
  else if (gasValue > 1500 && temp < 40.0) {
    if(currentState != ALARM_GAS && currentState != ALARM_FIRE) {
      currentState = ALARM_GAS;
      Serial.println("\n[FSM] ===> NGUY HIEM: RO RI GAS! Cam bat may bom! <===");
      Blynk.logEvent("gas_alert", "RÒ RỈ GAS! Đã mở thông gió, cấm bật bơm!");
      
      manualPumpState = false;
      Blynk.virtualWrite(V4, 0);
    }
    windowShouldBeOpen = true; 
  }
  // LOGIC 3: NẤU ĂN 
  else if (gasValue > 800 && tempRate < FIRE_TEMP_RATE && pirState == HIGH) {
    if(currentState == NORMAL) {
      currentState = WARN_COOKING;
      Serial.println("\n[FSM] -> Phat hien khoi bep (Nau an)");
      Blynk.logEvent("smoke_alert", "Phát hiện khói bếp.");
    }
  }
  // TRỞ VỀ BÌNH THƯỜNG
  else if (gasValue < 800 && flameValue == HIGH) { 
    if(currentState != NORMAL) {
      currentState = NORMAL;
      isMuted = false; 
      Serial.println("\n[FSM] -> He thong an toan, tro ve BINH THUONG.");
      
      manualPumpState = false;
      Blynk.virtualWrite(V4, 0);
    }
    windowShouldBeOpen = false; 
  }
}

// --- 3. Xử lý phần cứng đầu ra ---
void handleActuators() {
  
  // 3.1. QUẢN LÝ SERVO THÔNG MINH
  if (windowShouldBeOpen) {
    if (!isWindowOpen && !isWindowOpening) {
      Serial.println("[Servo] Bat dau quay MO cua so...");
      isWindowOpening = true;
      isWindowClosing = false;
      windowMoveStartTime = millis(); 
      windowServo.write(0);           // Quay mở
    }
    else if (isWindowOpening && (millis() - windowMoveStartTime >= 2000)) {
      windowServo.write(90);          // Dừng
      isWindowOpening = false;
      isWindowOpen = true;
      Serial.println("[Servo] Da MO cua so thanh cong!");
    }
  } 
  else { 
    if ((isWindowOpen || isWindowOpening) && !isWindowClosing) {
      Serial.println("[Servo] Bat dau quay DONG cua so...");
      isWindowClosing = true;
      isWindowOpening = false;
      windowMoveStartTime = millis(); 
      windowServo.write(180);         // Quay đóng
    }
    else if (isWindowClosing && (millis() - windowMoveStartTime >= 2000)) {
      windowServo.write(90);          // Dừng
      isWindowClosing = false;
      isWindowOpen = false;
      Serial.println("[Servo] Da DONG cua so thanh cong!");
    }
  }

  // 3.2. QUẢN LÝ MÁY BƠM
  if (manualPumpState && currentState != ALARM_GAS) {
    digitalWrite(RELAY_PIN, LOW);  // Bật bơm
  } else {
    digitalWrite(RELAY_PIN, HIGH); // Tắt bơm
  }

  // 3.3. QUẢN LÝ CÒI, ĐÈN 
  if (currentState == ALARM_FIRE) {
    if (!isMuted) {
      digitalWrite(BUZZER_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  } 
  else if (currentState == ALARM_GAS) {
    if (!isMuted) {
      digitalWrite(BUZZER_PIN, (millis() % 1000 < 500) ? HIGH : LOW); 
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
  else {
    digitalWrite(BUZZER_PIN, LOW); 
    digitalWrite(LED_PIN, LOW);    
  }
}

// --- 4. Nút nhấn tắt còi khẩn cấp ---
void checkMuteButton() {
  if (digitalRead(BTN_MUTE_PIN) == LOW) {
    if(!isMuted) Serial.println("[Button] Da nhan nut TAT COI khan cap!");
    isMuted = true;
  }
}

// --- BLYNK: QUẢN LÝ NÚT NHẤN TỪ APP (V4) ---
BLYNK_WRITE(V4) {
  int btnValue = param.asInt();
  Serial.print("[Blynk App] Nhan nut bom. Gia tri: "); 
  Serial.println(btnValue);
  
  if (currentState == ALARM_GAS) {
    Blynk.virtualWrite(V4, 0); 
    manualPumpState = false;
    Serial.println("[Blynk App] -> TU CHOI LENH: Dang ro ri GAS, chong no!");
  } else {
    manualPumpState = (btnValue == 1);
  }
}