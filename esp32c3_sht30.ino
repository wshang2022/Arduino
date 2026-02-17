/*
Board: ESP32-C3 OLED development board with 0.42 inch OLED (ESP32C3FN4/FH4)
4M Flash
https://www.aliexpress.us/item/3256807670573244.html This screen is different from Other 0.42 -inch screen. 
     The starting point of the screen is 12864 (13, 14)
I2C assignment
  - OLED    -> 0x3C ( ESP32-C3 does not use SPI for display)
  - SHT30   -> 0x44 (or 0x45)
  - ADS1115 -> 0x48 (or 0x48 - 0x4B)
  - BME280  -> 0x76 (or 0x77)
*/
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_SHT31.h>
#define OLED_X_OFFSET 28
#define OLED_Y_OFFSET 24

// U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0);
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);
Adafruit_SHT31 sht30 = Adafruit_SHT31();  // I2C ID is default to 44.

void setup() {
  Serial.begin(115200);
  Wire.begin(5, 6); // Shared with OLED I2C
  u8g2.begin();
  u8g2.setFont(u8g2_font_7x14_tr);
  u8g2.setFontPosTop();
  Serial.println("Probing SHT30");
  if (!sht30.begin(0x44)) {
    while (1);
  }
  Serial.println("SHT30 is ready");
  delay(2000);
}

void loop() {
  float temp = sht30.readTemperature();
  float hum = sht30.readHumidity()-4.5;
  Serial.printf("Temperature=%4.2f, Humidity=%4.2f\n", temp, hum);
  u8g2.firstPage();
  do {
    u8g2.drawStr(OLED_X_OFFSET + 0, OLED_Y_OFFSET + 5, "Temp:");
    u8g2.setCursor(OLED_X_OFFSET + 31, OLED_Y_OFFSET + 5);
    u8g2.print(temp, 1);
    u8g2.print(" C");
    u8g2.drawStr(OLED_X_OFFSET, OLED_Y_OFFSET + 20, "Hum :");
    u8g2.setCursor(OLED_X_OFFSET + 31, 45);
    u8g2.print(hum, 1);
    u8g2.print(" %");
  } while (u8g2.nextPage());
  delay(2000);
}
