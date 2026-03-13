#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>

// --- OLED Settings ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Sensor & Pin Settings ---
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define SOIL_PIN 34
#define SERVO_PIN 13

const int AIR_VALUE = 4095;  
const int WATER_VALUE = 400; 

// --- Servo Settings ---
Servo pinchServo;
const int PINCHED = 110;
const int UNPINCHED = 90;

// --- Web Server & State Variables ---
WebServer server(80);
const char* ssid = ""; //DESIRED SSID IS REQUIRED HERE
const char* password = ""; //PASSWORD FOR CONNECTING TO WI-FI

bool isAutoMode = true;
bool isMotorRunning = false;
bool manualWaterRequest = false;
unsigned long manualWaterTimer = 0;

float currentTemp = 0.0;
float currentHum = 0.0;
int waterPercent = 0;

// --- Auto Watering Variables ---
bool autoWatering = false;
unsigned long autoWaterTimer = 0;
const unsigned long AUTO_WATER_DURATION = 8000;  // Run motor for 8 seconds
const unsigned long AUTO_COOLDOWN = 10000;       // Wait 10 seconds for water to seep in

// --- Animation Variables ---
unsigned long lastBlinkTime = 0;
bool isBlinking = false;
const int BLINK_INTERVAL = 3500; 
const int BLINK_DURATION = 150;  

void setup() {
  Serial.begin(115200);

  dht.begin();
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED failed"));
    for(;;);
  }
  
  ESP32PWM::allocateTimer(0);
  pinchServo.setPeriodHertz(50);
  pinchServo.attach(SERVO_PIN, 500, 2400);
  pinchServo.write(PINCHED);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");

  server.on("/", handleRoot);
  server.on("/toggle", handleToggleMode);
  server.on("/water", handleManualWater);
  server.begin();
}

void loop() {
  server.handleClient();
  
  currentTemp = dht.readTemperature();
  currentHum = dht.readHumidity();
  
  int rawSoil = analogRead(SOIL_PIN);
  waterPercent = map(rawSoil, AIR_VALUE, WATER_VALUE, 0, 100);
  waterPercent = constrain(waterPercent, 0, 100);

  // --- Updated Auto Mode Logic ---
  if (isAutoMode) {
    // Check if it needs water, is not currently watering, AND the cooldown has passed
    if (waterPercent < 20 && !autoWatering && (millis() - autoWaterTimer > AUTO_COOLDOWN)) {
      autoWatering = true;
      autoWaterTimer = millis(); // Start the 5-second watering timer
    }

    if (autoWatering) {
      isMotorRunning = true;
      pinchServo.write(UNPINCHED);
      
      // If 5 seconds have passed, stop watering and start the cooldown phase
      if (millis() - autoWaterTimer > AUTO_WATER_DURATION) {
        autoWatering = false;
        autoWaterTimer = millis(); // Reset timer to track the 10-second cooldown
      }
    } else {
      isMotorRunning = false;
      pinchServo.write(PINCHED);
    }
  } else {
    // --- Manual Mode Logic (Unchanged) ---
    if (manualWaterRequest) {
      isMotorRunning = true;
      pinchServo.write(UNPINCHED);
      if (millis() - manualWaterTimer > 8000) {
        manualWaterRequest = false;
      }
    } else {
      isMotorRunning = false;
      pinchServo.write(PINCHED);
    }
  }

  display.clearDisplay();
  drawLeftSide();
  drawRightSide();
  display.display();
}

// --- Custom Icon Functions ---
void drawWaterDrop(int x, int y) {
  display.fillTriangle(x, y+2, x - 4, y + 8, x + 4, y + 8, SSD1306_WHITE);
  display.fillCircle(x, y + 8, 4, SSD1306_WHITE);
}

void drawThermometer(int x, int y) {
  display.drawRoundRect(x, y, 6, 12, 3, SSD1306_WHITE);
  display.fillCircle(x + 3, y + 12, 4, SSD1306_WHITE);
}

void drawCloud(int x, int y) {
  display.fillCircle(x + 4, y + 6, 3, SSD1306_WHITE);
  display.fillCircle(x + 8, y + 4, 4, SSD1306_WHITE);
  display.fillCircle(x + 12, y + 6, 3, SSD1306_WHITE);
  display.fillRect(x + 4, y + 6, 8, 4, SSD1306_WHITE);
}

void drawModeIcon(int x, int y, bool autoMode) {
  display.drawRect(x, y+2, 12, 12, SSD1306_WHITE);
  display.setCursor(x + 4, y + 4);
  display.setTextSize(1);
  display.print(autoMode ? "A" : "M");
}

// --- Display Functions ---
void drawLeftSide() {
  display.setTextColor(SSD1306_WHITE);
  
  // 1. Water (Row 1)
  drawWaterDrop(4, 0); 
  display.setTextSize(2); 
  display.setCursor(18, 0);
  display.print(waterPercent); display.print("%");

  // 2. Temp (Row 2)
  drawThermometer(2, 16);
  display.setTextSize(2);
  display.setCursor(18, 16);
  display.print((int)currentTemp); display.print("C"); 

  // 3. Humidity (Row 3)
  drawCloud(0, 35);
  display.setTextSize(2);
  display.setCursor(18, 32);
  display.print((int)currentHum); display.print("%");
  
  // 4. Mode (Row 4)
  drawModeIcon(2, 48, isAutoMode);
  display.setTextSize(2);
  display.setCursor(18, 48);
  display.print(isAutoMode ? "AUTO" : "MAN");
}

void drawRightSide() {
  int cx = 96;
  int cy = 32;
  bool isHappy = !(currentTemp > 35.0 || currentTemp < 15.0 || currentHum < 30.0 || waterPercent < 20.0);

  // --- Blink Timer Logic ---
  if (!isBlinking && (millis() - lastBlinkTime > BLINK_INTERVAL)) {
    isBlinking = true;
    lastBlinkTime = millis();
  } else if (isBlinking && (millis() - lastBlinkTime > BLINK_DURATION)) {
    isBlinking = false;
    lastBlinkTime = millis();
  }

  // --- Draw Eyes ---
  if (isBlinking) {
    display.drawLine(cx - 16, cy - 8, cx - 8, cy - 8, SSD1306_WHITE); 
    display.drawLine(cx + 8, cy - 8, cx + 16, cy - 8, SSD1306_WHITE); 
  } else {
    display.fillCircle(cx - 12, cy - 8, 4, SSD1306_WHITE);
    display.fillCircle(cx + 12, cy - 8, 4, SSD1306_WHITE);
  }

  // --- Draw Mouth & Straw Logic ---
  if (isMotorRunning) {
    // Make an "O" shaped mouth for drinking
    display.drawCircle(cx, cy + 10, 4, SSD1306_WHITE);
    display.fillCircle(cx, cy + 10, 2, SSD1306_BLACK);

    // Draw the straw extending from the mouth to the bottom edge
    int strawX1 = cx + 2; 
    int strawY1 = cy + 12;
    int strawX2 = 120; // Angles toward the bottom right corner
    int strawY2 = 64;
    display.drawLine(strawX1, strawY1, strawX2, strawY2, SSD1306_WHITE);
    display.drawLine(strawX1 - 1, strawY1, strawX2 - 1, strawY2, SSD1306_WHITE); // Make straw thick

    // Animate the water moving up the straw
    int animPos = (millis() / 150) % 6; 
    for (int i = animPos; i <= 20; i += 6) {
       int dropX = map(i, 0, 20, strawX2, strawX1);
       int dropY = map(i, 0, 20, strawY2, strawY1);
       display.fillCircle(dropX, dropY, 1, SSD1306_BLACK); // Creates a moving visual gap
    }
  } else {
    // Normal Mouth (Smile or Frown)
    if (isHappy) {
      display.drawCircle(cx, cy + 8, 8, SSD1306_WHITE); 
      display.fillRect(cx - 9, cy, 19, 9, SSD1306_BLACK); 
    } else {
      display.drawCircle(cx, cy + 16, 8, SSD1306_WHITE);
      display.fillRect(cx - 9, cy + 16, 19, 9, SSD1306_BLACK); 
    }
  }
}

// --- Web Server Functions ---
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>"; // THIS FIXES THE EMOJI GIBBERISH!
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='2'>";
  html += "<title>Smart Plant Care</title>";
  
  html += "<style>";
  html += "body { background-color: #121212; color: #e0e0e0; font-family: 'Segoe UI', Roboto, Helvetica, sans-serif; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }";
  html += ".card { background-color: #1e1e1e; border-radius: 16px; box-shadow: 0 10px 20px rgba(0,0,0,0.5); padding: 25px; width: 100%; max-width: 350px; margin-bottom: 20px; }";
  html += "h2 { color: #4CAF50; text-align: center; margin-top: 0; font-size: 24px; }";
  html += "h3 { text-align: center; margin-top: 0; color: #ffffff; }";
  html += ".metric { display: flex; justify-content: space-between; padding: 12px 0; border-bottom: 1px solid #333; font-size: 18px; }";
  html += ".metric:last-child { border-bottom: none; }";
  html += ".val { font-weight: bold; color: #81c784; }"; 
  html += ".status { text-align: center; margin-top: 20px; padding: 12px; border-radius: 8px; font-weight: bold; letter-spacing: 1px; }";
  html += ".status.on { background-color: #1565c0; color: white; }"; 
  html += ".status.off { background-color: #333; color: #888; }";    
  html += "button { width: 100%; padding: 15px; margin-top: 15px; border: none; border-radius: 8px; font-size: 16px; font-weight: bold; cursor: pointer; transition: 0.2s; }";
  html += ".btn-mode { background-color: #ff9800; color: #fff; }";
  html += ".btn-mode:hover { background-color: #e68a00; }";
  html += ".btn-water { background-color: #4CAF50; color: #fff; }";
  html += ".btn-water:hover { background-color: #45a049; }";
  html += "</style></head><body>";

  html += "<div class='card'>";
  html += "<h2>🌿 Plant Dashboard</h2>";
  html += "<div class='metric'><span>💧 Water</span> <span class='val'>" + String(waterPercent) + "%</span></div>";
  html += "<div class='metric'><span>🌡️ Temp</span> <span class='val'>" + String(currentTemp, 1) + " &deg;C</span></div>";
  html += "<div class='metric'><span>☁️ Humidity</span> <span class='val'>" + String(currentHum, 1) + "%</span></div>";
  
  if (isMotorRunning) {
    html += "<div class='status on'>💦 WATERING...</div>";
  } else {
    html += "<div class='status off'>IDLE</div>";
  }
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Control Panel</h3>";
  
  String modeColor = isAutoMode ? "#ff9800" : "#2196F3";
  html += "<div class='metric'><span>Current Mode:</span> <span style='color:" + modeColor + "; font-weight:bold;'>" + String(isAutoMode ? "AUTO" : "MANUAL") + "</span></div>";
  
  html += "<a href='/toggle' style='text-decoration:none;'><button class='btn-mode'>Switch Mode</button></a>";
  
  if (!isAutoMode) {
    html += "<a href='/water' style='text-decoration:none;'><button class='btn-water'>💧 Water Plant Now</button></a>";
  }
  
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleToggleMode() {
  isAutoMode = !isAutoMode;
  manualWaterRequest = false; 
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleManualWater() {
  if (!isAutoMode) {
    manualWaterRequest = true;
    manualWaterTimer = millis();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}
