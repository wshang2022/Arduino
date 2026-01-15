#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BluetoothSerial.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define XPT2046_CS 33
#define XPT2046_CLK 25
#define XPT2046_MISO 39
#define XPT2046_MOSI 32
#define SCREEN_WIDTH 240
#define SCREEN_HIGHT 320
#define SD_CS 5
#define LED_RED 4
#define LED_GREEN 16
#define LED_BLUE 17

bool isMuted = false;
unsigned long lastActionTime = 0;
const unsigned long idleTimeout = 20000;  // 20 Seconds

TFT_eSPI tft = TFT_eSPI();
BluetoothSerial SerialBT;
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS);
BLEScan* pBLEScan;

// --- LED Helpers ---
void setLED(bool r, bool g, bool b) {
  digitalWrite(LED_RED, !r);
  digitalWrite(LED_GREEN, !g);
  digitalWrite(LED_BLUE, !b);
}

void updateStatusLED() {
  float v = readBattery();

  if (v > 4.25) {
    // If voltage is very high, it's likely plugged into USB/Charging
    setLED(0, 0, 1);  // Blue for charging
  } else if (v < 3.5) {
    setLED(1, 0, 0);  // Red for Low Battery
  } else {
    digitalWrite(LED_RED, HIGH);  // Off
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);
  }
}

void policeStrobe() {
  if (isMuted) return;  // Exit immediately if muted
  for (int i = 0; i < 10; i++) {
    // Red ON, Blue OFF
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_BLUE, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    delay(80);

    // Blue ON, Red OFF
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_BLUE, LOW);
    digitalWrite(LED_GREEN, HIGH);
    delay(80);
  }
  // Turn all off when done
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}


void drawUI() {
  tft.drawFastHLine(0, 260, 240, TFT_WHITE);
  tft.drawFastVLine(80, 260, 60, TFT_WHITE);
  tft.drawFastVLine(160, 260, 60, TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString("SCAN", 40, 285, 2);
  tft.drawCentreString("LOGS", 120, 285, 2);

  if (isMuted) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString("MUTED", 200, 285, 2);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("ALARM", 200, 285, 2);
  }
}

void doScan() {
  lastActionTime = millis();
  setLED(0, 1, 0);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 10);
  tft.setTextColor(TFT_GREEN);
  tft.println("1. CLASSIC BT SCAN...");

  // --- Part 1: Classic BT ---
  BTScanResults* results = SerialBT.getScanResults();
  SerialBT.discover(4000);

  for (int j = 0; j < results->getCount(); j++) {
    auto device = results->getDevice(j);
    String name = device->getName().c_str();
    if (name.length() == 0) name = "Unknown";
    int rssi = device->getRSSI();

    // 1. Calculate Bar Width for 240px screen (Max bar width 200px)
    int barWidth = map(rssi, -90, -30, 0, 200);
    barWidth = constrain(barWidth, 0, 200);

    // 2. Pick Color based on Danger (Restored your exact logic)
    uint16_t barColor = TFT_GREEN;
    if (rssi > -60) barColor = TFT_YELLOW;  // Getting closer
    if (rssi > -45) barColor = TFT_RED;     // EXTREMELY CLOSE (Skimmer range)

    // 3. Print Text
    tft.setTextColor(TFT_CYAN);
    tft.printf("%s [%d]\n", name.substring(0, 18).c_str(), rssi);

    // 4. Draw the Signal Bar
    // We capture the current Y, draw the bar, then move the cursor down manually
    int currentY = tft.getCursorY();
    tft.drawRect(10, currentY, 200, 8, TFT_DARKGREY);   // The track
    tft.fillRect(10, currentY, barWidth, 8, barColor);  // The signal

    // Move cursor down so the next device name doesn't hit this bar
    tft.setCursor(0, currentY + 12);

    // --- THE ALARM TRIGGER ---
    if (name.indexOf("HC-05") >= 0 || name.indexOf("HC-06") >= 0) {
      tft.setTextColor(TFT_RED, TFT_YELLOW);
      tft.println("!!! SKIMMER DETECTED !!!");
      logToSD("ALARM", name, device->getAddress().toString().c_str(), rssi);
      policeStrobe();
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    }
  }
  tft.println("\n2. BLE SCAN...");
  pBLEScan->clearResults();
  BLEScanResults* bleResults = pBLEScan->start(5, false);

  for (int i = 0; i < bleResults->getCount(); i++) {
    BLEAdvertisedDevice device = bleResults->getDevice(i);
    int rssi = device.getRSSI();

    // 1. FILTER: Only show strong signals
    if (rssi < -80) continue;

    // 2. NAME LOGIC: Priority Name > UUID > MAC
    String displayName = "";
    if (device.haveName()) {
      displayName = device.getName().c_str();
    } else if (device.haveServiceUUID()) {
      displayName = "ID: " + String(device.getServiceUUID().toString().c_str()).substring(0, 8);
    } else {
      displayName = device.getAddress().toString().c_str();
    }

    tft.setTextColor(rssi > -50 ? TFT_RED : TFT_WHITE);
    tft.printf("%s [%d]\n", displayName.substring(0, 20).c_str(), rssi);

    int y = tft.getCursorY();
    int barWidth = constrain(map(rssi, -90, -30, 0, 180), 0, 180);
    tft.drawRect(10, y, 180, 6, TFT_DARKGREY);
    tft.fillRect(10, y, barWidth, 6, (rssi > -50) ? TFT_RED : TFT_GREEN);
    tft.setCursor(0, y + 10);
  }

  setLED(0, 0, 0);
  drawUI();
  lastActionTime = millis();  // Reset timer AFTER scan finishes
}

float readBattery() {
  // Read the ADC value (0 - 4095)
  int raw = analogRead(35);

  // Convert to voltage
  // (raw / 4095) * 3.3V * 2 (divider ratio)
  float voltage = (raw / 4095.0) * 3.3 * 2.0;

  // Calibration: If your multimeter says 4.0V but the screen says 3.8V,
  // adjust this offset until they match.
  float calibrationOffset = 0.12;
  return voltage + calibrationOffset;
}

void drawBattery() {
  int raw = analogRead(35);
  float voltage = (raw / 4095.0) * 3.3 * 2.0;
  float v = voltage + 0.12; 
  int percent = constrain(map(v * 100, 320, 420, 0, 100), 0, 100);

  // bx is the left edge of the battery icon
  int bx = 200; 
  int by = 5;

  // 1. Clear a smaller area to avoid flickering
  tft.fillRect(bx - 40, by, 75, 15, TFT_BLACK);

  // 2. Draw Battery Icon
  tft.drawRect(bx, by, 30, 12, TFT_WHITE);   
  tft.fillRect(bx + 30, by + 3, 3, 6, TFT_WHITE); 

  // 3. Fill based on level
  uint16_t color = (percent < 20) ? TFT_RED : (percent < 50) ? TFT_YELLOW : TFT_GREEN;
  int barWidth = map(percent, 0, 100, 0, 26);
  tft.fillRect(bx + 2, by + 2, barWidth, 8, color);

  // 4. Print percentage HUGGING the icon
  // Adjust the 'minus' value here to move the text closer or further.
  // -35 to -38 usually makes it sit right against the white border.
  tft.setCursor(bx - 36, by + 2); 
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.printf("%3d%%", percent); // %3d ensures alignment for 1, 2, or 3 digits
}
void logToSD(String type, String name, String mac, int rssi) {
  File file = SD.open("/log.csv", FILE_APPEND);
  if (file) {
    // We use millis() as a simple timestamp, or you could add NTP time later
    file.printf("%lu,%s,%s,%s,%d\n", millis(), type.c_str(), name.c_str(), mac.c_str(), rssi);
    file.close();
    Serial.println("Logged to SD");
  } else {
    Serial.println("Fail to open log.csv");
  }
}

void showLogs() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(0, 0);
  tft.println("--- LOG HISTORY ---");

  if (!SD.exists("/log.csv")) {
    tft.println("No logs found.");
  } else {
    File file = SD.open("/log.csv");
    // Simple logic: Read the file and print lines
    // To be efficient, we just show the most recent ones
    int count = 0;
    while (file.available() && count < 8) {
      String line = file.readStringUntil('\n');
      tft.setTextSize(1);
      tft.println(line);
      tft.println("-");
      count++;
    }
    file.close();
  }

  tft.setTextColor(TFT_CYAN);
  tft.println("\n[Tap anywhere to go BACK]");

  // Wait for touch to exit
  delay(500);
  while (!touch.touched()) { delay(10); }
  tft.fillScreen(TFT_BLACK);
}

void setup() {
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  // Backlight pin for most CYD boards is 21
  // Format: ledcAttach(pin, frequency, resolution);
  ledcAttach(21, 5000, 8);
  // To set brightness: 0 = Off, 255 = Full
  ledcWrite(21, 150);

  setLED(0, 0, 0);
  // Initialize SD Card
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card Mount Failed");
  } else {
    File file = SD.open("/log.csv", FILE_APPEND);
    if (!file) {
      File file = SD.open("/log.csv", FILE_WRITE);
      file.println("Timestamp,Type,Name,MAC,RSSI");
      file.close();
    }
  }

  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSPI);
  touch.setRotation(0);

  SerialBT.begin("CYD_Skimmer_Hunter");
  
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  
  // 1. Move Title down to Y=30 to leave Y=0-20 for the Battery
  tft.drawCentreString("SKIMMER SCANNER", 120, 30, 2); 
  
  // 2. Move the status text lower so it doesn't feel cramped
  tft.setCursor(0, 70);
  tft.println("Ready. Tap SCAN to begin...");

  // 3. Draw UI and Battery
  drawUI();      // Draws buttons
  drawBattery(); // Draws icon and hugging percentage
  lastActionTime = millis();
}

void loop() {
  // 1. Check for Auto-Rescan
  if (millis() - lastActionTime > idleTimeout) {
    doScan();
  }

  // 2. Check for Touch
  if (touch.touched()) {
    TS_Point p = touch.getPoint();

    if (p.z > 500) {
      // swap the mapping: use p.x for x and p.y for y
      // If SCAN/ALARM are reversed, we simply don't subtract from 240
      int x = map(p.x, 200, 3700, 0, 240);
      int y = map(p.y, 240, 3800, 0, 320);

      // --- VERIFICATION PRINT ---
      // When you touch SCAN, this should print X < 80.
      // When you touch ALARM/MUTE, this should print X > 160.
      Serial.printf("DEBUG -> X: %d  Y: %d  (RawX: %d RawY: %d)\n", x, y, p.x, p.y);

      if (y > 260) {
        if (x < 80) {  // Far Left
          doScan();
        } else if (x < 160) {  // Middle
          showLogs();
          drawUI();
        } else {  // Far Right
          isMuted = !isMuted;
          drawUI();
          delay(250);
        }
        lastActionTime = millis();
      }
    }
  }
  int px = map(millis() - lastActionTime, 0, idleTimeout, 0, 240);
  tft.fillRect(0, 257, px, 3, TFT_BLUE);
  tft.fillRect(px, 257, 240 - px, 3, TFT_BLACK);
  // 4. BATTERY UPDATE (Put it here!)
  // This ensures the battery is always updated regardless of touch or scans
  drawBattery();

  delay(10);  // Small delay to keep the CPU cool and touch stable
}