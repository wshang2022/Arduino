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

* Potential Conflict Warning
   GPIO 4 (the Red LED) is sometimes shared with the SD Card's internal logic or the LCD Backlight on certain revisions of the CYD.
	If your SD card stops working when the Red LED turns on, you may need to avoid using the Red pin and stick to Green/Blue.
	If your screen flickers when the LED flashes, it's a power draw issue; adding a small capacitor to the battery line usually fixes this.

* If your CYD reboots during the strobe, it means your battery can't provide enough instantaneous current. The Fix: Solder a small 100µF or 220µF electrolytic capacitor across the +/- power pins on the board. This acts like a "battery buffer" to handle the LED flashes. 

* To solder a voltage divider for your 18650 battery, you will be creating a "bridge" that allows the ESP32 to safely sample the battery's voltage. Since an 18650 can reach 4.2V and the ESP32 pins are only rated for 3.3V, the divider acts as a translator.
1. PreparationYou will need
  -- Two 100K Ohm resistors (The 1:1 ratio is easiest for math).
  -- Thin hookup wire (30AWG or similar).
  --Heat shrink tubing (to prevent shorts inside your CYD case).
2. The Soldering Steps 
Step A: Create the "Center Tap"Twist one leg of Resistor A and one leg of Resistor B together.Solder this junction.Solder a wire to this junction—this is your Signal Wire that goes to the ESP32.
Step B: Connect to PowerSolder the free leg of Resistor A to the Battery Positive (+) terminal or the positive solder pad on your board where the battery enters.Solder the free leg of Resistor B to the Battery Negative (-) or any GND pad on the ESP32.
Step C: Connect to the ESP32. Take the Signal Wire from the "Center Tap" and solder it to GPIO 35 (or Pin 35 on the P3 header).
3. Safety Check. CRITICAL: Use a multimeter before turning the device on. Measure the voltage between the Signal Wire and GND. It should be exactly half of your battery's voltage. If the battery is 4V, you should see 2V on the Signal Wire.

* Where Skimmers "Hide" in the Data
Keep an eye out for these "Red Flags" in your logs:

The "Unnamed" Strong Signal: If you see a device with no name but an RSSI of -35 to -45, and that signal stays strong only when you are touching the pump, that is suspicious even if it isn't named "HC-05."

The 24/7 Device: If you log the same MAC address at 2:00 AM and 2:00 PM at the same location, it's a fixed part of the infrastructure (or a skimmer).

The "Manufacturer" Lead: Real skimmers often use cheap modules from Guangzhou HC Information Technology. If your log shows a MAC address starting with 00:14:03, that is the specific OUI for HC-05 modules.

🛠 Hardware Configuration (Portrait Mode)
This project is optimized for the ESP32-2432S028R (Cheap Yellow Display). Due to the hardware mounting of the resistive touch layer, the following mapping is required for Portrait orientation:

Orientation: tft.setRotation(0) & touch.setRotation(0)

Coordinate Mapping:

X-Axis: Swapped to p.x and inverted: 240 - map(p.x, 200, 3700, 0, 240)

Y-Axis: Swapped to p.y: map(p.y, 240, 3800, 0, 320)

Touch Precision: Minimum pressure threshold p.z > 500 to prevent "Ghost Touches" common on these resistive panels.

🛡️ Log Bomb Protection (Linux-Style Rotation)
To prevent the SD card filesystem from filling up or slowing down the UI, the device implements an automatic Log Rotation strategy:

Silent Background Truncation: Inside logToSD(), if the file exceeds 10KB, the oldest 50 entries are automatically purged.

UI-Driven Truncation: When viewing logs, if the count exceeds 100 lines, the file is truncated to the most recent 20 entries to ensure fast rendering.

Persistence: Uses a temp.csv atomic write pattern to prevent log corruption during power loss.

🔋 UI Layout
Battery Icon: Positioned at (200, 5) with dynamic color coding (Green > 50%, Yellow > 20%, Red < 20%).

Hugging Logic: Percentage text is right-aligned to "hug" the battery icon for a clean status bar look.

Button Grid: Three-column layout at the bottom (y > 260) for Scan, Logs, and Mute/Alarm toggle.

References:
1. https://randomnerdtutorials.com/cheap-yellow-display-esp32-2432s028r/
f

