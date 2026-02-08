/* 
Board: M5StickCPlus2
https://docs.m5stack.com/en/arduino/m5unified/speaker_class#config
G25 => GUVA S
3.3V only. DO NOT USE 5V even GUVA support 5V ESP32 ADC cannot accept 5 V analog

*/
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
//#include "M5StickCPlus2.h"
#include "esp_bt.h"

// -------- CONFIG --------
#define UV_ADC_PIN 26
#define MEASURE_DURATION_MS 10000
#define LOOP_DELAY_MS 500
#define BTN_WAKE_GPIO GPIO_NUM_37  // M5 button on Plus2

#define AUTO_POWEROFF_MS 60000  // 60 seconds
unsigned long lastActivity = 0;

// Buzzer
#define BUZZER_PIN 2
#define BUZZER_CH 0
#define BUZZER_FREQ 3000  // 3 kHz
#define BUZZER_RES 8

// -------- COLORS (RGB565) --------
#define UV_GREEN 0x07E0
#define UV_YELLOW 0xFFE0
#define UV_ORANGE 0xFD20
#define UV_RED 0xF800
#define UV_PURPLE 0x780F

// -------- STATE --------
bool measuring = false;
unsigned long activeUntil = 0;
float uvi_min = 100.0;
float uvi_max = 0.0;

// -------- HELPERS --------
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
  // lastActivity = millis();
}

void goToSleep() {
  Serial.println("Entering deep sleep...");

  // Turn off display cleanly
  M5.Display.sleep();
  M5.Display.setBrightness(0);

  delay(100);

  // Wake on M5 button (active LOW)
  esp_sleep_enable_ext0_wakeup(BTN_WAKE_GPIO, 0);

  esp_deep_sleep_start();
}

// void buzzerOn(int freq) {
//   ledcWriteTone(BUZZER_CH, freq);
// }

// void buzzerOff() {
//   ledcWriteTone(BUZZER_CH, 0);
// }


void handleBuzzer(float uvi) {
  static unsigned long lastBeep = 0;

  if (uvi >= 5.0) {
    if (millis() - lastBeep > 1000) {
      M5.Speaker.tone(1200, 80);  // 80 ms chirp
      lastBeep = millis();
    }
  }
}


// -------- SETUP --------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  // Buzzer
  // ledcAttach(BUZZER_PIN, 3000, 8);  // pin, freq, resolution
  // Power savings (safe)
  WiFi.mode(WIFI_OFF);
  btStop();
  setCpuFrequencyMhz(80);

  // Display
  M5.Display.setRotation(3);  // Landscape
  M5.Display.setTextDatum(middle_center);

  // ADC
  analogReadResolution(12);
  analogSetPinAttenuation(UV_ADC_PIN, ADC_0db);

  Serial.begin(115200);
  delay(500);
  screenOn();
  M5.Display.clear(BLACK);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.setTextSize(1);
  M5.Display.drawString("Press M5 button to measure", M5.Display.width() / 2, 20);
  M5.Speaker.setVolume(255);
  delay(3000);
  screenOff();
  Serial.println("Idle. Press M5 button to measure.");
  lastActivity = millis();
}

// -------- LOOP --------
void loop() {
  M5.update();

  // ---- Start measurement ----
  if (!measuring && M5.BtnA.wasPressed()) {
    lastActivity = millis();

    measuring = true;
    activeUntil = millis() + MEASURE_DURATION_MS;
    uvi_min = 100.0;
    uvi_max = 0.0;

    screenOn();
    Serial.println("Measurement started");
  }

  // ---- Idle (screen off) ----
  if (!measuring) {
    delay(100);  // low-power idle
    return;
  }

  // ---- Measurement timeout ----
  if (millis() > activeUntil) {
    measuring = false;
    screenOff();
    Serial.println("Measurement finished");
    return;
  }

  // ---- Read sensor ----
  int raw = analogRead(UV_ADC_PIN);
  float voltage = raw * (1.1 / 4095.0);
  float uvi = voltage / 0.1;

  if (uvi < uvi_min) uvi_min = uvi;
  if (uvi > uvi_max) uvi_max = uvi;
  handleBuzzer(uvi);
  delay(1000);
  uint32_t color = uviColor(uvi);

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
  float bat = M5.Power.getBatteryVoltage();
  M5.Display.setTextSize(1);
  M5.Display.drawString(String(bat, 2) + "V", w - 40, h - 20);
  Serial.printf("RAW=%d  V=%.3f  UVI=%.2f\n", raw, voltage, uvi);
  // ---- Auto power off ----
  if (millis() - lastActivity > AUTO_POWEROFF_MS) {
    Serial.println("Powering off...");
    delay(100);
    goToSleep();
    //M5.Power.powerOff();
  }
  delay(LOOP_DELAY_MS);
}
