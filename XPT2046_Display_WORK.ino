/*
Board: ESP32-2432S028R CYD
Libraries: TFT_eSPI and XPT2046_Touchscreen
Rotation 1: Landscape (USB-C on the right)
Rotation 2: Portrait (USB-C at the top)
Rotation 3: Landscape (USB-C on the left)
Rotation 0: Portrait (USB-C at the bottom)
libraries/TFT_eSPI/User_Setup.h
#define ILI9341_2_DRIVER     // Alternative ILI9341 driver, see https://github.com/Bodmer/TFT_eSPI/issues/1172
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define SPI_FREQUENCY 27000000
*/

#include <SPI.h>
#include <TFT_eSPI.h>             // Graphics library
#include <XPT2046_Touchscreen.h>  // Touch library

// 1. Define Touch Pins (Separate from Display)
#define XPT2046_CS 33
#define XPT2046_CLK 25
#define XPT2046_MISO 39
#define XPT2046_MOSI 32

// 2. Objects for both systems
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS);

void setup() {
  Serial.begin(115200);
  pinMode(21, OUTPUT);
  // Set up PWM for the backlight
  ledcAttach(21, 5000, 8);  // Pin 21, 5kHz frequency, 8-bit resolution
  ledcWrite(21, 150);       // Set brightness (0-255)
  //digitalWrite(21, HIGH); // Full brgithness

  // Initialize Display
  tft.init();
  tft.invertDisplay(true); // Changes white back to black, etc.
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.drawCentreString("CYD Touch Test", 160, 10, 2);

  // Initialize Touch SPI (This avoids the conflict)
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSPI);
  touch.setRotation(3);  // Match screen rotation

  Serial.println("Setup Complete. Draw on the screen!");
}

void loop() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();

    // 3. Mapping Raw values to Pixels
    // Based on your test values (~1600/2000), these ranges are safe
    // USBC on the left rotation=1
    // int16_t x = map(p.x, 200, 3700, 320, 0);
    // int16_t y = map(p.y, 240, 3800, 240, 0);

    // USBC on the right rotation=3
    int16_t x = map(p.x, 200, 3700, 0, 320);
    int16_t y = map(p.y, 240, 3800, 0, 240);

    // 4. Draw on screen (Confirms no conflict)
    tft.fillCircle(x, y, 2, TFT_YELLOW);
    Serial.printf("X:%d  Y:%d\n", x, y);
  }
}