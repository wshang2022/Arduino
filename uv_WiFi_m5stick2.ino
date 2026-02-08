/* 
Board: M5StickCPlus2
https://docs.m5stack.com/en/arduino/m5unified/speaker_class#config
G25 => GUVA S
3.3V only. DO NOT USE 5V even GUVA support 5V ESP32 ADC cannot accept 5 V analog

*/
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
//#include "M5StickCPlus2.h"
#include "esp_bt.h"

// -------- CONFIG --------
#define UV_ADC_PIN 32
#define LOOP_DELAY_MS 16000

// -------- COLORS (RGB565) --------
#define UV_GREEN 0x07E0
#define UV_YELLOW 0xFFE0
#define UV_ORANGE 0xFD20
#define UV_RED 0xF800
#define UV_PURPLE 0x780F

String url = "https://api.thingspeak.com/update?api_key=XXXXXXX&field1=";

float uvi_min = 100.0;
float uvi_max = 0.0;
// WiFi credentials
String ssid = "SSID";
String password = "";

// -------- HELPERS --------
void connectWiFi() {
  // tft.fillScreen(BG_COLOR);
  // tft.setTextColor(TEXT_WHITE, BG_COLOR);
  // tft.setTextSize(1);
  // tft.setCursor(100, 115);
  // tft.print("Connecting WiFi...");
  Serial.println("Connecting WiFi...");

  if (ssid.length() == 0) {
    // tft.fillScreen(BG_COLOR);
    // tft.setTextColor(DOWN_COLOR, BG_COLOR);
    // tft.setCursor(80, 115);
    // tft.print("No WiFi config found!");
    Serial.println("No WiFi credentials in config.ini");
    delay(5000);
    return;
  }

  WiFi.begin(ssid.c_str(), password.c_str());

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
}

uint32_t uviColor(float uvi) {
  if (uvi >= 11) return UV_PURPLE;
  if (uvi >= 8) return UV_RED;
  if (uvi >= 6) return UV_ORANGE;
  if (uvi >= 3) return UV_YELLOW;
  return UV_GREEN;
}

void screenOff() {
  M5.Display.clear(BLACK);
  M5.Display.sleep();
  M5.Display.setBrightness(0);
}

void screenOn() {
  M5.Display.wakeup();
  M5.Display.setBrightness(60);
  M5.Display.clear(BLACK);
}

void update_uv(float uvi) {
  HTTPClient http;
  // String result = String(int(uvi*100));
  String result = String(uvi);
  //Serial.println(result);
  String URL = url + result;
  Serial.println("URL=" + URL);
  http.begin(URL);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode == 200) {
    Serial.println("Sample # = "+http.getString());
  } else {
    Serial.print("Error: HTTP GET FAIL");
  }
  http.end();
}
// -------- SETUP --------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  //  setCpuFrequencyMhz(80);

  // Display
  M5.Display.setRotation(3);  // Landscape
  M5.Display.setTextDatum(middle_center);

  // ADC
  // analogReadResolution(12);
  // analogSetPinAttenuation(UV_ADC_PIN, ADC_0db);

  Serial.begin(115200);
  delay(500);
  screenOn();
  M5.Display.clear(BLACK);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.setTextSize(1);
  M5.Display.drawString("Starting...", M5.Display.width() / 2, 20);

  Serial.println("Idle. Press M5 button to measure.");
  connectWiFi();
}

// -------- LOOP --------
void loop() {
  M5.update();
  // ---- Read sensor ----
  int raw = analogRead(UV_ADC_PIN);
  float voltage = raw * (1.1 / 4095.0);
  float uvi = voltage / 0.1;

  uint32_t color = uviColor(uvi);
  Serial.printf("RAW=%d  V=%.3f  UVI=%.2f\n", raw, voltage, uvi);

  update_uv(uvi);
  // ---- Display ----
  int w = M5.Display.width();
  int h = M5.Display.height();

  M5.Display.clear(BLACK);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.drawString("UV INDEX", w / 2, 20);

  M5.Display.setTextSize(5);
  M5.Display.setTextColor(color, BLACK);
  M5.Display.drawString(String(uvi, 1), w / 2, h / 2 - 10);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.drawString("MIN " + String(uvi_min, 1), w * 0.25, h - 40);
  M5.Display.drawString("MAX " + String(uvi_max, 1), w * 0.75, h - 40);
  // float bat = M5.Power.getBatteryVoltage();
  // Serial.printf("BAT=%d  UV=%d\n",
  //             M5.Power.getBatteryVoltage(),
  //             analogRead(32));
  // M5.Display.setTextSize(2);
  // M5.Display.drawString(String(bat, 2) + "V", w - 50, h - 20);

  delay(LOOP_DELAY_MS);
}
