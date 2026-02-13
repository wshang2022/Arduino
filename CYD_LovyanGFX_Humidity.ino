/*
Board: CYD2432S028R from Amazon https://www.amazon.com/dp/B0CLR7MQ91?th=1
LGFX version: 1.2.19
Display SPI: ST7789
*/
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <Adafruit_SHT31.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#define XPT2046_CS 33
#define XPT2046_CLK 25
#define XPT2046_MISO 39
#define XPT2046_MOSI 32
#define X_OFFSET 20
#define WIPE_OFFSET 25


class LGFX : public lgfx::LGFX_Device {
  // lgfx::Panel_ST7789 _panel;
  lgfx::Panel_ILI9341 _panel;

  lgfx::Bus_SPI _bus;

public:
  LGFX() {
    // ---------- SPI BUS ----------
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;

      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      // cfg.pin_miso = 12;
      cfg.pin_miso = -1; /* disable MISO because there is no readback needed */
      cfg.freq_read = 0; /* Slightly reduces SPI overhead and avoid floating pin issue */
      cfg.pin_dc = 2;

      cfg.use_lock = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    // ---------- PANEL ----------
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.offset_rotation = 0;
      // Critical settings to fix mirroring
      cfg.invert = false;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.invert = true;
      cfg.rgb_order = false;  // BGR order for CYD
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      // cfg.bus_shared = false;  // ← recommended
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};

static LGFX tft;
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS);

float lastTemp = NAN;
float lastHum = NAN;
bool useFahrenheit = true;
uint8_t rotation = 1;
bool isFahrenheit = true;
int tempLabelY = 75;
int tempValueY;
int humLabelY = 155;
int humValueY;
int readingOffset = 40;  // used to set the y-offset down from the labels to print the readings of temperature and humidity

Adafruit_SHT31 sht31 = Adafruit_SHT31();

void drawStaticUI() {
  tft.fillScreen(TFT_BLACK);
  // drawGrid(); // For layout debug only
  int W = tft.width();

  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(lgfx::color565(200, 200, 200));

  // Title
  tft.setCursor(20, 30);
  tft.print("Environment");

  tft.drawFastHLine(X_OFFSET, 50, W - 40, lgfx::color565(60, 60, 60));

  // ---- Temperature ----
  tft.setTextColor(lgfx::color565(150, 150, 150));
  Serial.printf("Temperature Label: x=20 y=%d\n", tempLabelY);
  tft.setCursor(X_OFFSET, tempLabelY);
  tft.print("Temperature");

  tempValueY = tempLabelY + readingOffset;  // 40px BELOW label

  // ---- Humidity ----
  Serial.printf("Humidity Label: x=20 y=%d\n", humLabelY);
  tft.setCursor(X_OFFSET, humLabelY);
  tft.print("Humidity");

  humValueY = humLabelY + readingOffset;
  tft.drawRoundRect(10, 10, 300, 220, 5, lgfx::color565(60, 60, 60));
}
uint16_t temperatureToColor(float tempC) {
  // Clamp temperature range
  if (tempC < 0) tempC = 0;
  if (tempC > 40) tempC = 40;

  float ratio = tempC / 40.0;  // 0.0 → 1.0

  uint8_t r, g, b;

  if (ratio < 0.25) {
    // Blue → Cyan
    float t = ratio / 0.25;
    r = 0;
    g = 255 * t;
    b = 255;
  } else if (ratio < 0.5) {
    // Cyan → Green
    float t = (ratio - 0.25) / 0.25;
    r = 0;
    g = 255;
    b = 255 * (1 - t);
  } else if (ratio < 0.75) {
    // Green → Yellow
    float t = (ratio - 0.5) / 0.25;
    r = 255 * t;
    g = 255;
    b = 0;
  } else {
    // Yellow → Red
    float t = (ratio - 0.75) / 0.25;
    r = 255;
    g = 255 * (1 - t);
    b = 0;
  }
  Serial.printf(" lgfx::color565(%d,%d,%d", r, g, b);
  return lgfx::color565(r, g, b);
}

float toDisplayTemp(float c) {
  return useFahrenheit ? (c * 9.0 / 5.0 + 32.0) : c;
}

void drawTemperature(float c) {
  // uint16_t color = TFT_YELLOW;
  uint16_t color = temperatureToColor(c);
  float t = toDisplayTemp(c);
  if (t > 90.0) color = TFT_RED;
  else if (t < 65.0) color = TFT_DARKGREEN;

  tft.setFreeFont(&FreeSerifBold18pt7b);

  char buf[16];
  sprintf(buf, "%.1f", t);

  tft.setTextColor(color, TFT_BLACK);  // fg + bg
  tft.drawString(buf, X_OFFSET, tempValueY);

  // Unit
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(lgfx::color565(180, 180, 180), TFT_BLACK);
  tft.drawString(useFahrenheit ? "°F" : "°C", 90, tempValueY);
}

void drawHumidity(float h) {
  uint16_t color = TFT_GREEN;
  if (h > 40.0) color = TFT_RED;
  else if (h < 20.0) color = TFT_CYAN;

  tft.setFreeFont(&FreeSansBold18pt7b);

  char buf[16];
  sprintf(buf, "%.1f", h);

  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(buf, X_OFFSET, humValueY);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(lgfx::color565(180, 180, 180), TFT_BLACK);
  tft.drawString("%", 90, humValueY);
}


void redrawUI() {
  drawStaticUI();
  lastTemp = NAN;  // force redraw
  lastHum = NAN;
}

void drawGrid() {
  for (int i = 20; i < 240; i += 20) {
    tft.drawFastHLine(0, i, 320, lgfx::color565(160, 160, 160));
  }
  for (int i = 20; i < 320; i += 20) {
    tft.drawFastVLine(i, 0, 240, lgfx::color565(160, 160, 160));
  }
}


bool touchPressed() {
  static uint32_t lastTouch = 0;
  if (!touch.touched()) return false;

  uint32_t now = millis();
  if (now - lastTouch < 200) return false;  // debounce
  lastTouch = now;
  return true;
}

void redrawTemp() {

  float t = sht31.readTemperature();
  t = toDisplayTemp(t);

  tft.setFreeFont(&FreeSerifBold18pt7b);

  char buf[16];
  sprintf(buf, "%.1f", t);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);  // fg + bg
  tft.drawString(buf, X_OFFSET, tempValueY);

  // Unit
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(lgfx::color565(180, 180, 180), TFT_BLACK);
  tft.drawString(useFahrenheit ? "°F" : "°C", 90, tempValueY);
}


void mapTouchToScreen(const TS_Point &p, int16_t &x, int16_t &y) {
  x = map(p.x, 200, 3800, 0, 320);
  y = map(p.y, 200, 3800, 0, 240);
  // Serial.printf("Mapped x = %d y = %d\n", x, y);
}


void handleTouch() {
  if (!touchPressed()) return;
  TS_Point p = touch.getPoint();
  Serial.printf("x = %d y = %d z = %d\n", p.x, p.y, p.z);
  int16_t x, y;
  mapTouchToScreen(p, x, y);
  if (x > 80 && x < 120 && y > 120 && y < 160) {
    useFahrenheit = !useFahrenheit;
    // Serial.printf("handleTouch invoked useFahrenheit ? %d\n",useFahrenheit);
    redrawTemp();
    return;
  }
}

void setup() {
  // setCpuFrequencyMhz(80);
  Serial.begin(115200);
  Wire.begin(27, 22);  // SDA, SCL
  sht31.begin(0x44);
  // Enable backlight
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  tft.init();
  tft.setRotation(3);
  // This is only for ILI9341 DO NOT use this for 7789
  tft.startWrite();
  tft.writeCommand(0x36);  // MADCTL
  tft.writeData(0x48);     // MV | BGR (rotation 1 without MY bit)
  tft.endWrite();
  tft.invertDisplay(true);  // Fix color inversion
  
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSPI);
  touch.setRotation(3);
  tft.setTextDatum(lgfx::middle_left);
  drawStaticUI();
  Serial.printf("Screen width=%d height=%d\n",tft.width(),tft.height());
  Serial.println("Ready....");
}
void loop() {

  handleTouch();  // ALWAYS first

  static uint32_t lastRead = 0;
  if (millis() - lastRead > 2000) {
    lastRead = millis();

    float t = sht31.readTemperature();
    float h = sht31.readHumidity();

    if (t != lastTemp) drawTemperature(t);
    if (h != lastHum) drawHumidity(h);

    lastTemp = t;
    lastHum = h;
  }
}
