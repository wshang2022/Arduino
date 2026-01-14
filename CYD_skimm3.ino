//#include "BluetoothSerial.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BluetoothSerial.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"



// Pins for CYD
#define XPT2046_CS 33
#define XPT2046_CLK 25
#define XPT2046_MISO 39
#define XPT2046_MOSI 32
#define SD_CS 5  // Standard for most CYD boards

unsigned long lastActionTime = 0;
const unsigned long idleTimeout = 20000;  // 20 seconds in milliseconds

TFT_eSPI tft = TFT_eSPI();
BluetoothSerial SerialBT;
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS);
BLEScan* pBLEScan;

void drawUI() {
  // Draw the button divider at the bottom
  tft.drawFastHLine(0, 180, 320, TFT_WHITE);
  tft.drawFastVLine(160, 180, 60, TFT_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN);
  tft.drawCentreString("SCAN", 80, 200, 1);
  tft.drawCentreString("LOGS", 240, 200, 1);
  tft.setTextSize(1);  // ALWAYS reset to 1 after drawing UI
}

void drawBattery() {
  // Replace 35 with whichever pin your battery divider is connected to
  // Common pins on CYD are 34 (if no LDR) or 35 (on the P3 header)
  int raw = analogRead(35);

  // Conversion: Raw ADC (0-4095) to Voltage
  // Formula: (Raw / 4095) * RefVoltage * DividerFactor
  float voltage = (raw / 4095.0) * 3.3 * 2.0;

  // Map voltage to percentage (3.2V = 0%, 4.2V = 100%)
  int percent = map(voltage * 100, 320, 420, 0, 100);
  percent = constrain(percent, 0, 100);

  // Draw Icon
  tft.drawRect(280, 5, 30, 12, TFT_WHITE);  // Battery body
  tft.fillRect(310, 8, 3, 6, TFT_WHITE);    // Battery tip

  // Fill color based on level
  uint16_t color = TFT_GREEN;
  if (percent < 20) color = TFT_RED;
  else if (percent < 50) color = TFT_YELLOW;

  int barWidth = map(percent, 0, 100, 0, 26);
  tft.fillRect(282, 7, barWidth, 8, color);

  // Print text next to icon
  tft.setCursor(240, 7);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.printf("%d%%", percent);
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

void doScan() {
  lastActionTime = millis();
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println("1. SCANNING CLASSIC...");

  // --- Part 1: Classic BT ---

  BTScanResults* results = SerialBT.getScanResults();
  SerialBT.discover(3000);  // Shorter scan to save radio time
  for (int j = 0; j < results->getCount(); j++) {
    auto device = results->getDevice(j);
    int rssi = device->getRSSI();
    int barWidth = map(rssi, -90, -30, 0, 200);
    barWidth = constrain(barWidth, 0, 200);  // Keep it within bounds
    // 2. Pick Color based on Danger
    uint16_t barColor = TFT_GREEN;
    if (rssi > -60) barColor = TFT_YELLOW;  // Getting closer
    if (rssi > -45) barColor = TFT_RED;     // EXTREMELY CLOSE (Skimmer range)
    tft.setTextColor(TFT_CYAN);
    tft.printf("%s [%d]\n", device->getName().c_str(), rssi);
    tft.drawRect(10, tft.getCursorY(), 200, 8, TFT_DARKGREY);
    tft.fillRect(10, tft.getCursorY(), barWidth, 8, barColor);
  }

  // --- Crucial: Give the radio a moment to switch modes ---
  delay(200);
  tft.setTextColor(TFT_GREEN);
  tft.println("\n2. SCANNING BLE...");

  // --- Part 2: BLE ---
  // Ensure previous results are cleared from memory
  pBLEScan->clearResults();

  // Try a slightly longer window for better pickup
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);

  BLEScanResults* bleResults = pBLEScan->start(5, false);  // Scan for 5 seconds

  int bleCount = bleResults->getCount();
  if (bleCount == 0) {
    tft.setTextColor(TFT_YELLOW);
    tft.println("No BLE found (Try again)");
  } else {
    tft.printf("BLE Count: %d\n", bleCount);
    for (int i = 0; i < bleCount; i++) {
      BLEAdvertisedDevice device = bleResults->getDevice(i);
      int rssi = device.getRSSI();
      if (rssi < -80) continue;
      // 1. Calculate Bar Width (Screen is 320px wide)
      // We map -90 -> 0px and -30 -> 200px (leaving room for text)
      int barWidth = map(rssi, -90, -30, 0, 200);
      barWidth = constrain(barWidth, 0, 200);  // Keep it within bounds

      // 2. Pick Color based on Danger
      uint16_t barColor = TFT_GREEN;
      if (rssi > -60) barColor = TFT_YELLOW;  // Getting closer
      if (rssi > -45) barColor = TFT_RED;     // EXTREMELY CLOSE (Skimmer range)

      // Check if Name exists
      String name = "???";
      if (device.haveName()) {
        name = device.getName().c_str();
      } else if (device.haveServiceUUID()) {
        name = "UUID: " + String(device.getServiceUUID().toString().c_str()).substring(0, 8);
      } else {
        // Get the MAC address as a string (e.g., "aa:bb:cc:dd:ee:ff")
        name = device.getAddress().toString().c_str();
      }
      // If no name, check for a Service UUID (Common for sensors)

      if (rssi > -50) {
        tft.setTextColor(TFT_RED);  // Too close!
      } else {
        tft.setTextColor(TFT_WHITE);
      }
      tft.printf("%s [%d]\n", name.c_str(), rssi);
      // 4. Draw the Signal Bar
      // Draw an empty gray box as the background (the "track")
      tft.drawRect(10, tft.getCursorY(), 200, 8, TFT_DARKGREY);
      // Fill the box based on signal strength
      tft.fillRect(10, tft.getCursorY(), barWidth, 8, barColor);
      tft.println("\n");  // Move cursor down for next device
      if (rssi > -50) {
        //tft.setTextColor(TFT_RED);
        logToSD("BLE", name.c_str(), device.getAddress().toString().c_str(), rssi);
        //tft.print("!! LOGGED !! ");
      }
      if (i > 10) break;
    }
  }

  pBLEScan->clearResults();
  tft.setTextColor(TFT_GREEN);
  // tft.println("\n[Tap to Scan Again]");
  tft.println("\n[Tap or wait 20s to Rescan]");
  // Update the timer again after the scan finishes (scans take ~10 seconds)
  lastActionTime = millis();
}

void setup() {
  Serial.begin(115200);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);  // Backlight
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
  tft.setRotation(3);
  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSPI);
  touch.setRotation(3);

  SerialBT.begin("CYD_Skimmer_Hunter");
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawCentreString("SKIMMER SCANNER", 160, 10, 2);
  tft.setCursor(0, 50);
  tft.println("Tap SCAN to begin...");
  drawUI();  //Redraw button after scan finishes
  // Initialize BLE
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);  // Active scan gets more data from devices
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}
void loop() {
  unsigned long now = millis();

  // 1. Check for Auto-Rescan
  if (now - lastActionTime > idleTimeout) {
    doScan();
    drawUI();  //Redraw button after scan finishes
  }

  // 2. Manual Touch Logic
  if (touch.touched()) {
    TS_Point p = touch.getPoint();

    // Map touch to pixels
    int x = map(p.x, 200, 3700, 0, 320);
    int y = map(p.y, 240, 3800, 0, 240);

    if (y > 180) {
      if (x < 160) {
        doScan();
        drawUI();
      } else {
        showLogs();
        drawUI();  // Redraw buttons after returning from logs
        lastActionTime = millis();
      }
    }
    delay(300);
  }

  // 3. Progress Bar Logic
  unsigned long timeElapsed = millis() - lastActionTime;
  if (timeElapsed < idleTimeout) {
    int progressWidth = map(timeElapsed, 0, idleTimeout, 0, 320);
    // Draw the bar in the "Dead Zone" between the buttons and the results
    tft.fillRect(0, 175, progressWidth, 4, TFT_BLUE);
    tft.fillRect(progressWidth, 175, 320 - progressWidth, 4, TFT_BLACK);
  }
  // drawBattery();
}
