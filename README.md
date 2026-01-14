* The CYD board is purchased from Amazon https://www.amazon.com/dp/B0CLR7MQ91?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1
* It has well-know issues
  1. "Inverted Color" or "Reverse Display"
  2. The touchscreen function of TFT_eSPI's library by Bodmer does not work
  3. Use XPT2046_Touchscreen by Paul Stoffregen for touch screen.
  4. Use ILI9341_2_DRIVER in libraries/TFT_eSPI/User_Setup.h instead so that the whole screen can be used

* CYD_skimm3.ino: Bluetooth classic and BLE scan for BT skikmmer
* For cording reference with CYD
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

References:
1. https://randomnerdtutorials.com/cheap-yellow-display-esp32-2432s028r/
