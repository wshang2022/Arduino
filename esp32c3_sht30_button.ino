/*
https://github.com/olikraus/u8g2/wiki/u8g2reference#setfont
https://github.com/olikraus/u8g2/wiki/fntlistallplain#u8g2-font-list
https://github.com/olikraus/u8g2/wiki/u8g2reference#clear
_mf, _mr, _mn refer to medium glyph sets optimized for memory usage.
_tn = digits only
_tr = full ASCII (most useful)
_tf = full ASCII with background fill

u8g2_font_<family><size>_<style>

Suffix	Meaning	Includes
_tr	transparent ASCII	letters, numbers, symbols
_tf	full ASCII (filled background)	same as tr but draws background
_tn	tabular numbers	digits only
_tm	numbers + punctuation	digits + . - :
_mf	medium font, full glyph	extended character set
_mr	medium font, reduced glyph	fewer characters
_mn	medium font, numbers only	digits only

Use case	  Font
Big digits	_tn
Full text	  _tr
Overwrite text	_tf
Minimal memory	_mr
Numbers + punctuation	_tm
*/
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_SHT31.h>
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 1000;


// OLED (GPIO6=SCL, GPIO5=SDA)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0); // Share GPIO5, GPIO6 with SHT30

// SHT30
Adafruit_SHT31 sht30 = Adafruit_SHT31();

// button
#define BTN 9  // change if needed

// display mode
int mode = 0;  // 0=C, 1=F, 2=Humidity

// offsets discovered earlier
#define XOFF 28
#define YOFF 24

void setup() {
  Serial.begin(115200);
  pinMode(BTN, INPUT_PULLUP);
  Wire.begin(5, 6);
  u8g2.begin();
  if (!sht30.begin(0x44)) {
    Serial.println("SHT30 not found");
  }
}

void drawBig(String value, String unit) {
  u8g2.clearBuffer();

  // large number font
  u8g2.setFont(u8g2_font_logisoso32_tf);
  int w = u8g2.getStrWidth(value.c_str());
  u8g2.drawStr((72 - w) / 2 + XOFF - 2, 40 + YOFF, value.c_str());
  // small unit label
  u8g2.setFont(u8g2_font_7x14B_tf);
  if (mode == 2) {
    u8g2.setFont(u8g2_font_logisoso24_tf);
    u8g2.drawStr(58 + XOFF, 34 + YOFF, unit.c_str());  // %
  } else {
    u8g2.drawStr(36 + XOFF, 14 + YOFF, unit.c_str()); // °C °F
  }
  u8g2.sendBuffer();
}

void loop() {

  // ---------- BUTTON HANDLING ----------
  static bool lastState = HIGH;
  bool current = digitalRead(BTN);

  if (lastState == HIGH && current == LOW) {
    mode++;
    if (mode > 2) mode = 0;
  }

  lastState = current;

  // ---------- UPDATE DISPLAY ----------
  if (millis() - lastUpdate >= updateInterval) {
    lastUpdate = millis();

    float t = sht30.readTemperature();
    float h = sht30.readHumidity();
    h -= 4.5;
    if (!isnan(t)) {

      if (mode == 0) {
        drawBig(String(t, 1), "\260C");
        // drawBig(String(t, 1), "C");
      } else if (mode == 1) {
        float f = t * 9.0 / 5.0 + 32.0;
        drawBig(String(f, 1), "\260F");
        // drawBig(String(f, 1), "F");
      } else {
        drawBig(String(h, 0), "%");
      }
    }
  }
}

void _loop() {

  // detect button press
  static bool lastState = HIGH;
  bool current = digitalRead(BTN);

  if (lastState == HIGH && current == LOW) {
    mode++;
    if (mode > 2) mode = 0;
    delay(200);  // debounce
  }
  lastState = current;
  float t = sht30.readTemperature();
  float h = sht30.readHumidity();

  if (!isnan(t)) {
    if (mode == 0) {
      drawBig(String(t, 1), "C");
    } else if (mode == 1) {
      float f = t * 9.0 / 5.0 + 32.0;
      drawBig(String(f, 1), "F");
    } else {
      drawBig(String(h, 0), "%");
    }
  } else {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0 + XOFF, 10 + YOFF, "Sensor error");
    u8g2.sendBuffer();
  }
  delay(1000);
}
