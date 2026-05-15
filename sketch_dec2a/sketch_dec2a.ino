#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// 3 NÚT (D3, D5, D6)
#define BTN_1 0   
#define BTN_2 14  
#define BTN_3 12  

// Biến lưu trạng thái nút
int lastState1 = HIGH;
int lastState2 = HIGH;
int lastState3 = HIGH;

// Biến lưu thông số PC
int cpuVal = 0;
int ramVal = 0;
unsigned long lastButtonPress = 0; // Để tính thời gian hiện thông báo

void setup() {
  Serial.begin(115200);
  
  pinMode(BTN_1, INPUT_PULLUP);
  pinMode(BTN_2, INPUT_PULLUP);
  pinMode(BTN_3, INPUT_PULLUP);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  
  // Màn hình chờ ban đầu
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(10, 20);
  display.print("CONNECTING");
  display.display();
}

void loop() {
  // 1. ĐỌC DỮ LIỆU TỪ PC (Dạng: "C:50|R:80")
  if (Serial.available() > 0) {
    String data = Serial.readStringUntil('\n');
    data.trim(); // Xóa khoảng trắng thừa

    // Phân tích dữ liệu
    int splitIndex = data.indexOf('|');
    if (splitIndex > 0) {
      String cpuPart = data.substring(0, splitIndex); // C:50
      String ramPart = data.substring(splitIndex + 1); // R:80
      
      // Lấy số sau dấu :
      cpuVal = cpuPart.substring(2).toInt();
      ramVal = ramPart.substring(2).toInt();
    }
  }

  // 2. HIỂN THỊ (Nếu vừa bấm nút thì ko vẽ Stats, để yên cho hiện chữ Open)
  if (millis() - lastButtonPress > 1000) {
    drawStats(cpuVal, ramVal);
  }

  // 3. XỬ LÝ NÚT BẤM (Chống Spam + Gửi Lệnh)
  int s1 = digitalRead(BTN_1);
  int s2 = digitalRead(BTN_2);
  int s3 = digitalRead(BTN_3);

  // Nút 1: FB
  if (s1 == LOW && lastState1 == HIGH) {
    Serial.println("OPEN_FB");
    showNotification("FACEBOOK");
    lastButtonPress = millis(); // Đánh dấu thời gian bấm
  }
  
  // Nút 2: YT
  if (s2 == LOW && lastState2 == HIGH) {
    Serial.println("OPEN_YT");
    showNotification("YOUTUBE");
    lastButtonPress = millis();
  }

  // Nút 3: ZALO
  if (s3 == LOW && lastState3 == HIGH) {
    Serial.println("OPEN_ZALO");
    showNotification("ZALO");
    lastButtonPress = millis();
  }

  lastState1 = s1;
  lastState2 = s2;
  lastState3 = s3;
}

// Hàm vẽ biểu đồ CPU/RAM
void drawStats(int cpu, int ram) {
  display.clearDisplay();
  
  // Tiêu đề
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("PC MONITOR");
  display.drawLine(0, 10, 128, 10, WHITE);

  // CPU
  display.setCursor(0, 15); display.print("CPU: "); display.print(cpu); display.print("%");
  display.drawRect(0, 25, 128, 8, WHITE);
  display.fillRect(2, 27, map(cpu, 0, 100, 0, 124), 4, WHITE);

  // RAM
  display.setCursor(0, 38); display.print("RAM: "); display.print(ram); display.print("%");
  display.drawRect(0, 48, 128, 8, WHITE);
  display.fillRect(2, 50, map(ram, 0, 100, 0, 124), 4, WHITE);

  display.display();
}

// Hàm hiện thông báo khi bấm nút
void showNotification(String app) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 10); display.print("OPENING...");
  display.setTextSize(2);
  display.setCursor(0, 30); display.print(app);
  display.display();
}