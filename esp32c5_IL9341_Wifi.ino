/*
ESP32-C5 pinout is correct on https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c5/esp32-c5-devkitc-1/user_guide.html
ILI9341 Pin out is correct VCC GND CS RESET DC MOSI SCK LED
The board is ESP32C5 Dev Module
esp32 cannot be 2.0.17 which is meant for Stock price code

ILI9341	ESP32-C5 (J1 is the top side with 3.3v and J3 is the GND side with USB facing down)
VCC		3V3
GND		GND
7 SCK		GPIO2 (J1 down 3 )
6 MOSI	GPIO3  (J1 down 4)
  MISO	GPIO26 (optional)
3 CS		GPIO5 (J3 down 9)
5 DC		GPIO7 (J1 down 8)
4 RST		GPIO4 (J3 down 8)
8 LED		3V3 (or GPIO via resistor)
*/
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

/* ===== TFT PINS ===== */
#define TFT_CS 5
#define TFT_DC 7
#define TFT_RST 4

SPIClass spi(FSPI);
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

/* ===== SETTINGS ===== */
#define MAX_NETWORKS 20
#define SCAN_INTERVAL 20000

struct WifiNet {
  String ssid;
  int rssi;
  int channel;
};

WifiNet nets[MAX_NETWORKS];
unsigned long lastScan = 0;

/* ===== UI ===== */

void _drawHeader() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_CYAN);
  tft.setCursor(10, 5);
  tft.println("ESP32-C5 WiFi Scan");
  tft.drawFastHLine(0, 30, 320, ILI9341_DARKGREY);
}
void drawHeader() {
  tft.fillRect(0, 0, 320, 30, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_CYAN);
  tft.setCursor(10, 5);
  tft.println("ESP32-C5 WiFi Scan");
  tft.drawFastHLine(0, 30, 320, ILI9341_DARKGREY);
}

uint16_t rssiColor(int rssi) {
  if (rssi > -55) return ILI9341_GREEN;
  if (rssi > -70) return ILI9341_YELLOW;
  return ILI9341_RED;
}

uint16_t GHZColor(int channel) {
  if (channel > 36) return ILI9341_RED;
  if (channel < 15) return ILI9341_GREEN;
  return ILI9341_GREEN;
}

int rssiBarWidth(int rssi) {
  rssi = constrain(rssi, -100, -30);
  return map(rssi, -100, -30, 10, 140);
}

/* ===== SORT ===== */
void sortByRSSI(int count) {
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (nets[j].rssi > nets[i].rssi) {
        WifiNet tmp = nets[i];
        nets[i] = nets[j];
        nets[j] = tmp;
      }
    }
  }
}

/* ===== SCAN ===== */
void scanAndDisplay() {
  drawHeader();
  // Clear ONLY scan area
  tft.fillRect(0, 31, 320, 209, ILI9341_BLACK);


  int n = WiFi.scanNetworks(false, true);
  if (n <= 0) {
    tft.setCursor(10, 40);
    tft.setTextColor(ILI9341_RED);
    tft.setTextSize(2);
    tft.println("No networks");
    return;
  }

  int count = min(n, MAX_NETWORKS);
  for (int i = 0; i < count; i++) {
    nets[i].ssid = WiFi.SSID(i);
    nets[i].rssi = WiFi.RSSI(i);
    nets[i].channel = WiFi.channel(i);
  }

  sortByRSSI(count);

  int y = 40;
  tft.setTextSize(1);

  for (int i = 0; i < count; i++) {
    uint16_t color = rssiColor(nets[i].rssi);
    int barW = rssiBarWidth(nets[i].rssi);

    // RSSI bar
    tft.fillRect(5, y, barW, 8, color);

    // Text
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(155, y);
    tft.printf("%4d dBm ",
               nets[i].rssi);
    tft.setTextColor(GHZColor(nets[i].channel));               
    tft.printf("CH%-2d",nets[i].channel);     

    // tft.printf("%4d dBm CH%-2d",
    //            nets[i].rssi, nets[i].channel);

    tft.setCursor(155, y + 9);
    tft.println(nets[i].ssid);

    y += 20;
    if (y > 220) break;
  }

  WiFi.scanDelete();
}

/* ===== SETUP ===== */
void setup() {
  Serial.begin(115200);

  spi.begin(2, 26, 3, TFT_CS);
  tft.begin();
  tft.setRotation(1);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(200);

  drawHeader();
}

/* ===== LOOP ===== */
void loop() {
  if (millis() - lastScan > SCAN_INTERVAL) {
    lastScan = millis();
    scanAndDisplay();
  }
}
