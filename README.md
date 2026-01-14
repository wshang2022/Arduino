* The CYD board is purchased from Amazon https://www.amazon.com/dp/B0CLR7MQ91?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1
* It has well-know issues
  1. "Inverted Color" or "Reverse Display"
  2. The touchscreen function of TFT_eSPI's library by Bodmer does not work
  3. Use XPT2046_Touchscreen by Paul Stoffregen for touch screen.
  4. Use ILI9341_2_DRIVER in libraries/TFT_eSPI/User_Setup.h instead so that the whole screen can be used

* CYD_skimm3.ino: Bluetooth classic and BLE scan for BT skikmmer

References:
1. https://randomnerdtutorials.com/cheap-yellow-display-esp32-2432s028r/
