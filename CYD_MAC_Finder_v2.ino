/*
 * CYD2432S02 — WiFi MAC Prefix Locator
 * Board   : ESP32 (CYD2432S02)
 * Display : TFT_eSPI  320×240  landscape
 * Touch   : XPT2046   VSPI hardware
 * Storage : LittleFS  (internal flash — no SD, no SPI conflicts)
 *
 * ── SPI buses ───────────────────────────────────────────────
 *  TFT   → HSPI  (TFT_eSPI User_Setup.h)
 *          MOSI=13 MISO=12 CLK=14 CS=15 DC=2
 *
 *  Touch → VSPI
 *          MOSI=32 MISO=39 CLK=25 CS=33 IRQ=36
 *
 * ── Libraries ───────────────────────────────────────────────
 *  TFT_eSPI            (Bodmer)          — Library Manager
 *  XPT2046_Touchscreen (Paul Stoffregen) — Library Manager
 *  LittleFS            (built-in ESP32 core, no install needed)
 *  WiFi                (built-in ESP32 core)
 *
 * ── Arduino IDE board setting ───────────────────────────────
 *  Tools → Partition Scheme → "Default 4MB with spiffs"
 *  (or any scheme that includes a LittleFS/SPIFFS partition)
 *
 * ── Touch calibration ───────────────────────────────────────
 *  x = map(p.x, 200, 3700, 0, 320)
 *  y = map(p.y, 240, 3800, 0, 240)
 *
 * ── Log file  /CYD_MAC.log  (LittleFS) ──────────────────────
 *  One entry per line:  AA:BB:CC:DD:EE:FF,NetworkName
 *  • Only non-hidden SSIDs stored
 *  • Dedup by full MAC address
 *  • Maximum 10 entries — oldest evicted when full
 *
 * ── Screens ─────────────────────────────────────────────────
 *  SCREEN_SCAN  keypad + continuous async WiFi scan + result
 *  SCREEN_LOG   list of saved entries; tap one → loads first
 *               3 hex pairs into MAC bar → back to SCREEN_SCAN

 * Touch (XPT2046) uses a custom VSPI on unusual pins:
    MOSI=32, MISO=39, CLK=25, CS=33
  SD Card uses the default VSPI pins:
    MOSI=23, MISO=19, CLK=18, CS=5
 * When you call SD.begin(), it reconfigures the VSPI host with the default pins, which breaks the touch SPI object 
   that was using the same host on different pins.
 */

// ============================================================
//  LIBRARIES
// ============================================================
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>

// ============================================================
//  PIN DEFINITIONS — Touch (VSPI)
// ============================================================
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25
#define TOUCH_CS 33
#define TOUCH_IRQ 36

// ============================================================
//  LOG FILE
// ============================================================
#define LOG_FILE "/CYD_MAC.log"
#define MAX_LOG_ENTRIES 10
#define SD_CS_PIN 5

// ============================================================
//  LAYOUT  (landscape 320 × 240)
//
//  SCREEN_SCAN:
//  Y=  0  ┌────────────────────────────────────────────────┐
//         │ MAC input bar                          H=36   │
//  Y= 44  ├────────────────────────┬───────────────────────┤
//         │ Keypad 4×4  X=8 W=228  │ Result panel X=244   │
//         │ row H=29, gap=3        │ W=68  H=148           │
//  Y=177  ├────────────────────────┤                       │
//         │ <<DEL  X=8  H=18       │                       │
//  Y=200  ├────────────────────────┴───────────────────────┤
//         │ [START/STOP]  [LOG(n)]  [RESET]       H=36   │
//  Y=240  └────────────────────────────────────────────────┘
//
//  SCREEN_LOG:
//  Y=  0  ┌────────────────────────────────────────────────┐
//         │ Title "Saved MACs"  n/10               H=30   │
//  Y= 30  ├────────────────────────────────────────────────┤
//         │ 10 × row H=18, gap=1  (tap → load + go SCAN)  │
//  Y=210  ├────────────────────────────────────────────────┤
//         │ [< BACK]                               H=28   │
//  Y=240  └────────────────────────────────────────────────┘
// ============================================================

// ── SCAN screen ─────────────────────────────────────────────
#define MAC_BAR_X 8
#define MAC_BAR_Y 4
#define MAC_BAR_W 304
#define MAC_BAR_H 36

#define KP_COLS 4
#define KP_ROWS 4
#define KP_X 8
#define KP_Y 48
#define KP_BTN_W 56
#define KP_BTN_H 29
#define KP_GAP 3
// KP bottom = 48 + 4*(29+3) - 3 = 173

#define BSP_X 8
#define BSP_Y 177
#define BSP_W 228
#define BSP_H 18
// BSP bottom = 195

#define BTN_Y 200
#define BTN_H 36
#define BTN_W 98
#define BTN_GAP 5
#define BTN_START_X 8
#define BTN_LOG_X (BTN_START_X + BTN_W + BTN_GAP)
#define BTN_RESET_X (BTN_LOG_X + BTN_W + BTN_GAP)
// BTN bottom = 236 ✓

#define RES_X 244
#define RES_Y 48
#define RES_W 68
#define RES_H 148

#define SPIN_CX (RES_X + RES_W / 2)
#define SPIN_CY (RES_Y + 12)
#define SPIN_INTERVAL 120

// ── LOG screen ───────────────────────────────────────────────
#define LOG_TITLE_Y 0
#define LOG_TITLE_H 30
#define LOG_ROW_X 4
#define LOG_ROW_Y 34
#define LOG_ROW_W 312
#define LOG_ROW_H 18
#define LOG_ROW_GAP 1
#define LOG_BACK_X 8
#define LOG_BACK_Y 212
#define LOG_BACK_W 304
#define LOG_BACK_H 26
// BACK bottom = 238 ✓

// ============================================================
//  COLOURS
// ============================================================
#define C_BG TFT_BLACK
#define C_PANEL 0x1082
#define C_KEY_NRM 0x2945
#define C_KEY_PRE 0x5ACB
#define C_KEY_TXT TFT_WHITE
#define C_MAC_BG 0x0841
#define C_MAC_TXT 0x07FF
#define C_START_BG 0x0600
#define C_STOP_BG 0x8000
#define C_LOG_BG 0x0439
#define C_RESET_BG 0x6000
#define C_BTN_TXT TFT_WHITE
#define C_BSP_BG 0x4B2A
#define C_NEAR TFT_GREEN
#define C_CLOSE TFT_YELLOW
#define C_FAR 0xFC00
#define C_NONE 0x4208
#define C_BORDER 0x528A
#define C_SPIN TFT_CYAN
#define C_ROW_NRM 0x2104
#define C_ROW_PRE 0x5ACB
#define C_ROW_TXT TFT_WHITE
#define C_TITLE_BG 0x0439
#define C_BACK_BG 0x2945

// ============================================================
//  KEYPAD  (all 16 hex digits; <<DEL is a separate button)
// ============================================================
const char KEYS[KP_ROWS][KP_COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '0', 'D', 'E', 'F' }
};

// ============================================================
//  TYPES  (all structs global — before any function)
// ============================================================
struct TouchPoint {
  int x;
  int y;
  bool pressed;
};

enum Screen { SCREEN_SCAN,
              SCREEN_LOG };
enum ScanState { STATE_IDLE,
                 STATE_SCANNING };

struct LogEntry {
  String mac;  // full AA:BB:CC:DD:EE:FF  (uppercase)
  String ssid;
};

// ============================================================
//  GLOBALS
// ============================================================
TFT_eSPI tft;

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

Screen currentScreen = SCREEN_SCAN;
ScanState scanState = STATE_IDLE;

String macInput = "";
String targetPrefix = "";

String resultMAC = "";
String resultSSID = "";
int resultRSSI = 0;
bool hasResult = false;

bool fsAvailable = false;

LogEntry logCache[MAX_LOG_ENTRIES];
int logCount = 0;
bool sdAvailable = false;
uint8_t spinFrame = 0;
uint32_t lastSpinMs = 0;
uint16_t scanCount = 0;
bool wasTouched = false;

// ============================================================
//  UTILITY
// ============================================================
bool inRect(int tx, int ty, int rx, int ry, int rw, int rh) {
  return tx >= rx && tx < rx + rw && ty >= ry && ty < ry + rh;
}

void drawBtn(int x, int y, int w, int h,
             uint16_t bg, uint16_t fg,
             const char* label, int sz = 2) {
  tft.fillRoundRect(x, y, w, h, 5, bg);
  tft.drawRoundRect(x, y, w, h, 5, C_BORDER);
  tft.setTextColor(fg, bg);
  tft.setTextSize(sz);
  int tw = strlen(label) * 6 * sz;
  int th = 8 * sz;
  tft.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
  tft.print(label);
}

// ============================================================
//  LITTLEFS — init
// ============================================================
void initFS() {
  // formatOnFail=true so first boot auto-formats the partition
  fsAvailable = LittleFS.begin(true);
  Serial.println(fsAvailable ? "LittleFS: mounted." : "LittleFS: failed.");
}

void initSD() {
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card Mount Failed");
    // tft.setTextColor(TFT_RED);
    // tft.drawString("SD Failed", 20, 200);
  } else {
    sdAvailable = true;
    Serial.println("SD exists");
    Serial.println("SD Card Initialized");
    SD.end();
    touchSPI.begin(25, 39, 32, TOUCH_CS);  // CLK, MISO, MOSI, CS
    touch.begin(touchSPI);

    // tft.setTextColor(TFT_GREEN);
    // tft.drawString("SD OK", 20, 200);
  }
  // if (SD.begin(SD_CS_PIN)) {
  //   sdAvailable = true;
  //   Serial.println("SD exists");
  // }
}

// ============================================================
//  LOG CACHE — load from LittleFS into RAM
// ============================================================
void loadLog() {
  logCount = 0;
  if (!fsAvailable) return;

  fs::File f = LittleFS.open(LOG_FILE, "r");
  if (!f) {
    Serial.println("Log: no file yet.");
    return;
  }

  while (f.available() && logCount < MAX_LOG_ENTRIES) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int comma = line.indexOf(',');
    if (comma < 0) continue;
    String mac = line.substring(0, comma);
    String ssid = line.substring(comma + 1);
    mac.trim();
    ssid.trim();
    if (mac.length() == 0 || ssid.length() == 0) continue;
    logCache[logCount].mac = mac;
    logCache[logCount].ssid = ssid;
    logCount++;
  }
  f.close();
  Serial.printf("Log: loaded %d entries.\n", logCount);
}

// ============================================================
//  LOG CACHE — write RAM → LittleFS
// ============================================================
void saveLog() {
  if (!fsAvailable) return;
  fs::File f = LittleFS.open(LOG_FILE, "w");
  if (!f) {
    Serial.println("Log: write failed.");
    return;
  }
  for (int i = 0; i < logCount; i++) {
    f.print(logCache[i].mac);
    f.print(',');
    f.println(logCache[i].ssid);
  }
  f.close();
  Serial.printf("Log: saved %d entries.\n", logCount);
}

// ============================================================
//  LOG CACHE — add entry (dedup by MAC, max 10, FIFO eviction)
// ============================================================
void logEntry(const String& mac, const String& ssid) {
  if (ssid.length() == 0) return;  // skip hidden SSIDs
  if (!fsAvailable) return;

  String macUp = mac;
  macUp.toUpperCase();

  // Update existing entry if MAC already known
  for (int i = 0; i < logCount; i++) {
    if (logCache[i].mac == macUp) {
      if (logCache[i].ssid != ssid) {
        logCache[i].ssid = ssid;
        saveLog();
      }
      return;
    }
  }

  // Evict oldest if full
  if (logCount >= MAX_LOG_ENTRIES) {
    for (int i = 0; i < MAX_LOG_ENTRIES - 1; i++)
      logCache[i] = logCache[i + 1];
    logCount = MAX_LOG_ENTRIES - 1;
  }

  logCache[logCount].mac = macUp;
  logCache[logCount].ssid = ssid;
  logCount++;
  saveLog();

  Serial.printf("Log: +%s / %s  (%d/%d)\n",
                macUp.c_str(), ssid.c_str(), logCount, MAX_LOG_ENTRIES);
}

// ============================================================
//  DRAW — SCAN screen
// ============================================================
void drawMacBar() {
  tft.fillRoundRect(MAC_BAR_X, MAC_BAR_Y, MAC_BAR_W, MAC_BAR_H, 5, C_MAC_BG);
  tft.drawRoundRect(MAC_BAR_X, MAC_BAR_Y, MAC_BAR_W, MAC_BAR_H, 5, C_BORDER);
  String disp = "";
  for (int i = 0; i < 6; i++) {
    disp += (i < (int)macInput.length()) ? macInput[i] : '_';
    if (i == 1 || i == 3) disp += ':';
  }
  tft.setTextColor(C_MAC_TXT, C_MAC_BG);
  tft.setTextSize(3);
  tft.setCursor(MAC_BAR_X + (MAC_BAR_W - (int)disp.length() * 18) / 2,
                MAC_BAR_Y + (MAC_BAR_H - 24) / 2);
  tft.print(disp);
}

void drawKeypad(bool locked = false) {
  for (int r = 0; r < KP_ROWS; r++) {
    for (int c = 0; c < KP_COLS; c++) {
      int x = KP_X + c * (KP_BTN_W + KP_GAP);
      int y = KP_Y + r * (KP_BTN_H + KP_GAP);
      char label[2] = { KEYS[r][c], 0 };
      drawBtn(x, y, KP_BTN_W, KP_BTN_H,
              locked ? C_NONE : C_KEY_NRM, C_KEY_TXT, label, 2);
    }
  }
}

void drawBackspace(bool locked = false) {
  drawBtn(BSP_X, BSP_Y, BSP_W, BSP_H,
          locked ? C_NONE : C_BSP_BG, C_KEY_TXT, "<< DEL", 1);
}

void drawScanButtons() {
  const char* startLabel = (scanState == STATE_SCANNING) ? "STOP" : "START";
  uint16_t startBg = (scanState == STATE_SCANNING) ? C_STOP_BG : C_START_BG;
  drawBtn(BTN_START_X, BTN_Y, BTN_W, BTN_H, startBg, C_BTN_TXT, startLabel, 1);
  char logLabel[8];
  snprintf(logLabel, sizeof(logLabel), "LOG(%d)", logCount);
  drawBtn(BTN_LOG_X, BTN_Y, BTN_W, BTN_H, C_LOG_BG, C_BTN_TXT, logLabel, 1);
  drawBtn(BTN_RESET_X, BTN_Y, BTN_W, BTN_H, C_RESET_BG, C_BTN_TXT, "RESET", 1);
}

void drawResultPanel() {
  tft.fillRoundRect(RES_X, RES_Y, RES_W, RES_H, 6, C_PANEL);
  tft.drawRoundRect(RES_X, RES_Y, RES_W, RES_H, 6, C_BORDER);

  // LittleFS indicator dot
  tft.fillCircle(RES_X + RES_W - 7, RES_Y + 7,
                 4, fsAvailable ? C_NEAR : C_NONE);

  tft.fillCircle(RES_X + RES_W - 7, RES_Y + 7 + 9,
                 4, sdAvailable ? TFT_YELLOW : C_NONE);

  if (scanState == STATE_IDLE && !hasResult) {
    tft.setTextColor(C_KEY_TXT, C_PANEL);
    tft.setTextSize(1);
    tft.setCursor(RES_X + 8, RES_Y + 60);
    tft.print("Press");
    tft.setCursor(RES_X + 6, RES_Y + 72);
    tft.print("START");
    return;
  }

  if (!hasResult) {
    tft.setTextColor(C_SPIN, C_PANEL);
    tft.setTextSize(1);
    tft.setCursor(RES_X + 4, RES_Y + 38);
    tft.print("Searching");
    char sc[8];
    snprintf(sc, sizeof(sc), "#%u", scanCount);
    tft.setTextColor(TFT_WHITE, C_PANEL);
    tft.setCursor(RES_X + (RES_W - (int)strlen(sc) * 6) / 2, RES_Y + 50);
    tft.print(sc);
    return;
  }

  // Distance colour + label
  uint16_t dc;
  const char* dl;
  if (resultRSSI >= -60) {
    dc = C_NEAR;
    dl = "NEAR";
  } else if (resultRSSI >= -75) {
    dc = C_CLOSE;
    dl = "CLOSE";
  } else {
    dc = C_FAR;
    dl = "FAR";
  }

  // Full MAC
  String m = resultMAC;
  m.toUpperCase();
  tft.setTextSize(1);
  tft.setTextColor(C_MAC_TXT, C_PANEL);
  tft.setCursor(RES_X + 3, RES_Y + 6);
  tft.print(m.substring(0, 8));
  tft.setCursor(RES_X + 3, RES_Y + 17);
  tft.print(m.substring(9));

  // SSID
  String sd2 = resultSSID;
  if ((int)sd2.length() > 10) sd2 = sd2.substring(0, 9) + "~";
  tft.setTextColor(TFT_WHITE, C_PANEL);
  tft.setCursor(RES_X + 3, RES_Y + 30);
  tft.print(sd2);

  // RSSI
  char rs[10];
  snprintf(rs, sizeof(rs), "%ddBm", resultRSSI);
  tft.setCursor(RES_X + (RES_W - (int)strlen(rs) * 6) / 2, RES_Y + 43);
  tft.print(rs);

  // Scan counter
  char sc[8];
  snprintf(sc, sizeof(sc), "#%u", scanCount);
  tft.setTextColor(C_NONE, C_PANEL);
  tft.setCursor(RES_X + (RES_W - (int)strlen(sc) * 6) / 2, RES_Y + 55);
  tft.print(sc);

  // Distance label
  tft.setTextColor(dc, C_PANEL);
  tft.setTextSize(2);
  tft.setCursor(RES_X + (RES_W - (int)strlen(dl) * 12) / 2, RES_Y + 68);
  tft.print(dl);

  // Signal bars
  int bars = 1;
  if (resultRSSI >= -50) bars = 4;
  else if (resultRSSI >= -60) bars = 3;
  else if (resultRSSI >= -75) bars = 2;
  int barBaseY = RES_Y + RES_H - 8;
  int barW = 10, barGap = 4;
  int startBX = RES_X + (RES_W - (4 * barW + 3 * barGap)) / 2;
  for (int b = 0; b < 4; b++) {
    int bh = 8 + b * 7;
    int bx = startBX + b * (barW + barGap);
    int by = barBaseY - bh;
    tft.fillRect(bx, by, barW, bh, (b < bars) ? dc : C_NONE);
    tft.drawRect(bx, by, barW, bh, C_BORDER);
  }
}

void drawSpinner() {
  if (scanState != STATE_SCANNING) return;
  uint32_t now = millis();
  if (now - lastSpinMs < SPIN_INTERVAL) return;
  lastSpinMs = now;
  const int8_t dx[8] = { 0, 7, 10, 7, 0, -7, -10, -7 };
  const int8_t dy[8] = { -10, -7, 0, 7, 10, 7, 0, -7 };
  uint8_t prev = (spinFrame + 7) % 8;
  uint8_t trail = (spinFrame + 6) % 8;
  tft.fillCircle(SPIN_CX + dx[prev], SPIN_CY + dy[prev], 2, C_PANEL);
  tft.fillCircle(SPIN_CX + dx[trail], SPIN_CY + dy[trail], 2, C_NONE);
  tft.fillCircle(SPIN_CX + dx[spinFrame], SPIN_CY + dy[spinFrame], 2, C_SPIN);
  spinFrame = (spinFrame + 1) % 8;
}

void drawScanScreen() {
  tft.fillScreen(C_BG);
  drawMacBar();
  drawKeypad(scanState == STATE_SCANNING);
  drawBackspace(scanState == STATE_SCANNING);
  drawScanButtons();
  drawResultPanel();
}

// ============================================================
//  DRAW — LOG screen
// ============================================================
void drawLogRow(int idx, bool highlighted = false) {
  int y = LOG_ROW_Y + idx * (LOG_ROW_H + LOG_ROW_GAP);
  uint16_t bg = highlighted ? C_ROW_PRE : C_ROW_NRM;
  tft.fillRect(LOG_ROW_X, y, LOG_ROW_W, LOG_ROW_H, bg);
  tft.drawRect(LOG_ROW_X, y, LOG_ROW_W, LOG_ROW_H, C_BORDER);

  if (idx >= logCount) {
    tft.setTextColor(C_NONE, bg);
    tft.setTextSize(1);
    tft.setCursor(LOG_ROW_X + 6, y + 5);
    tft.print("---");
    return;
  }
  // MAC in cyan
  tft.setTextColor(C_MAC_TXT, bg);
  tft.setTextSize(1);
  tft.setCursor(LOG_ROW_X + 4, y + 5);
  tft.print(logCache[idx].mac);
  // SSID
  String ssidDisp = logCache[idx].ssid;
  if ((int)ssidDisp.length() > 14) ssidDisp = ssidDisp.substring(0, 13) + "~";
  tft.setTextColor(C_ROW_TXT, bg);
  tft.setCursor(LOG_ROW_X + 112, y + 5);
  tft.print(ssidDisp);
}

void drawLogScreen() {
  tft.fillScreen(C_BG);

  // Title bar
  tft.fillRect(0, LOG_TITLE_Y, 320, LOG_TITLE_H, C_TITLE_BG);
  tft.drawLine(0, LOG_TITLE_H, 320, LOG_TITLE_H, C_BORDER);
  tft.setTextColor(TFT_WHITE, C_TITLE_BG);
  tft.setTextSize(2);
  tft.setCursor(8, LOG_TITLE_Y + 8);
  tft.print("Saved MACs");
  // count badge
  char cnt[8];
  snprintf(cnt, sizeof(cnt), "%d/%d", logCount, MAX_LOG_ENTRIES);
  tft.setTextSize(1);
  tft.setTextColor(C_MAC_TXT, C_TITLE_BG);
  tft.setCursor(280, LOG_TITLE_Y + 10);
  tft.print(cnt);

  // Column header
  tft.setTextColor(C_NONE, C_BG);
  tft.setTextSize(1);
  tft.setCursor(LOG_ROW_X + 4, LOG_ROW_Y - 9);
  tft.print("FULL MAC           SSID");

  // All 10 rows (empty slots show "---")
  for (int i = 0; i < MAX_LOG_ENTRIES; i++) drawLogRow(i);

  // BACK button
  drawBtn(LOG_BACK_X, LOG_BACK_Y, LOG_BACK_W, LOG_BACK_H,
          C_BACK_BG, C_BTN_TXT, "< BACK", 1);
}

// ============================================================
//  WIFI
// ============================================================
void buildTarget() {
  targetPrefix = "";
  targetPrefix += macInput[0];
  targetPrefix += macInput[1];
  targetPrefix += ':';
  targetPrefix += macInput[2];
  targetPrefix += macInput[3];
  targetPrefix += ':';
  targetPrefix += macInput[4];
  targetPrefix += macInput[5];
  targetPrefix.toUpperCase();
}

void startScanning() {
  if (macInput.length() < 6) return;
  buildTarget();
  scanState = STATE_SCANNING;
  hasResult = false;
  scanCount = 0;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  WiFi.scanNetworks(true, false);  // async, visible SSIDs only
  drawScanScreen();
  Serial.printf("Scanning for: %s\n", targetPrefix.c_str());
}

void stopScanning() {
  WiFi.scanDelete();
  scanState = STATE_IDLE;
  drawScanScreen();
}

void processScanResults() {
  int n = WiFi.scanComplete();
  if (n < 0) return;

  scanCount++;
  int bestRSSI = -9999;
  bool found = false;
  String fMAC, fSSID;
  int fRSSI = 0;

  for (int i = 0; i < n; i++) {
    String bssid = WiFi.BSSIDstr(i);
    bssid.toUpperCase();
    String ssid = WiFi.SSID(i);
    if (bssid.substring(0, 8) == targetPrefix && ssid.length() > 0) {
      int rssi = WiFi.RSSI(i);
      if (rssi > bestRSSI) {
        bestRSSI = rssi;
        fMAC = bssid;
        fSSID = ssid;
        fRSSI = rssi;
        found = true;
      }
    }
  }

  WiFi.scanDelete();

  if (found) {
    resultMAC = fMAC;
    resultSSID = fSSID;
    resultRSSI = fRSSI;
    hasResult = true;
    logEntry(resultMAC, resultSSID);
  } else if (hasResult) {
    resultRSSI = -99;
  }

  drawResultPanel();
  drawScanButtons();  // refresh LOG(n) badge

  if (scanState == STATE_SCANNING)
    WiFi.scanNetworks(true, false);
}

// ============================================================
//  TOUCH
// ============================================================
TouchPoint getTouch() {
  TouchPoint tp = { 0, 0, false };
  if (!touch.tirqTouched() || !touch.touched()) return tp;
  TS_Point p = touch.getPoint();
  tp.x = map(p.x, 200, 3700, 0, 320);
  tp.y = map(p.y, 240, 3800, 0, 240);
  tp.pressed = true;
  return tp;
}

void handleScanTouch(int tx, int ty) {
  // START / STOP
  if (inRect(tx, ty, BTN_START_X, BTN_Y, BTN_W, BTN_H)) {
    if (scanState == STATE_IDLE) {
      drawBtn(BTN_START_X, BTN_Y, BTN_W, BTN_H, C_KEY_PRE, C_BTN_TXT, "START", 1);
      delay(80);
      startScanning();
    } else {
      drawBtn(BTN_START_X, BTN_Y, BTN_W, BTN_H, C_KEY_PRE, C_BTN_TXT, "STOP", 1);
      delay(80);
      stopScanning();
    }
    return;
  }
  // LOG
  if (inRect(tx, ty, BTN_LOG_X, BTN_Y, BTN_W, BTN_H)) {
    char ll[8];
    snprintf(ll, sizeof(ll), "LOG(%d)", logCount);
    drawBtn(BTN_LOG_X, BTN_Y, BTN_W, BTN_H, C_KEY_PRE, C_BTN_TXT, ll, 1);
    delay(80);
    currentScreen = SCREEN_LOG;
    drawLogScreen();
    return;
  }
  // RESET
  if (inRect(tx, ty, BTN_RESET_X, BTN_Y, BTN_W, BTN_H)) {
    drawBtn(BTN_RESET_X, BTN_Y, BTN_W, BTN_H, C_KEY_PRE, C_BTN_TXT, "RESET", 1);
    delay(80);
    if (scanState == STATE_SCANNING) {
      WiFi.scanDelete();
      scanState = STATE_IDLE;
    }
    macInput = "";
    hasResult = false;
    scanCount = 0;
    resultMAC = "";
    resultSSID = "";
    resultRSSI = 0;
    drawScanScreen();
    return;
  }
  // Input locked while scanning
  if (scanState == STATE_SCANNING) return;
  // Backspace
  if (inRect(tx, ty, BSP_X, BSP_Y, BSP_W, BSP_H)) {
    drawBtn(BSP_X, BSP_Y, BSP_W, BSP_H, C_KEY_PRE, C_KEY_TXT, "<< DEL", 1);
    delay(80);
    drawBackspace(false);
    if (macInput.length() > 0) macInput.remove(macInput.length() - 1);
    drawMacBar();
    return;
  }
  // Hex keypad
  for (int r = 0; r < KP_ROWS; r++) {
    for (int c = 0; c < KP_COLS; c++) {
      int bx = KP_X + c * (KP_BTN_W + KP_GAP);
      int by = KP_Y + r * (KP_BTN_H + KP_GAP);
      if (!inRect(tx, ty, bx, by, KP_BTN_W, KP_BTN_H)) continue;
      char k = KEYS[r][c];
      char label[2] = { k, 0 };
      drawBtn(bx, by, KP_BTN_W, KP_BTN_H, C_KEY_PRE, C_KEY_TXT, label, 2);
      delay(80);
      drawBtn(bx, by, KP_BTN_W, KP_BTN_H, C_KEY_NRM, C_KEY_TXT, label, 2);
      if (macInput.length() < 6) macInput += k;
      drawMacBar();
    }
  }
}

void handleLogTouch(int tx, int ty) {
  // BACK
  if (inRect(tx, ty, LOG_BACK_X, LOG_BACK_Y, LOG_BACK_W, LOG_BACK_H)) {
    drawBtn(LOG_BACK_X, LOG_BACK_Y, LOG_BACK_W, LOG_BACK_H,
            C_KEY_PRE, C_BTN_TXT, "< BACK", 1);
    delay(80);
    currentScreen = SCREEN_SCAN;
    drawScanScreen();
    return;
  }
  // Tap a log row
  for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
    int ry = LOG_ROW_Y + i * (LOG_ROW_H + LOG_ROW_GAP);
    if (!inRect(tx, ty, LOG_ROW_X, ry, LOG_ROW_W, LOG_ROW_H)) continue;
    if (i >= logCount) break;
    drawLogRow(i, true);
    delay(100);
    drawLogRow(i, false);
    // Extract first 3 hex pairs: "AA:BB:CC:DD:EE:FF" → "AABBCC"
    String m = logCache[i].mac;
    macInput = "";
    macInput += m[0];
    macInput += m[1];  // AA
    macInput += m[3];
    macInput += m[4];  // BB
    macInput += m[6];
    macInput += m[7];  // CC
    Serial.printf("Log: loaded %s\n", macInput.c_str());
    currentScreen = SCREEN_SCAN;
    drawScanScreen();
    return;
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  tft.init();
  tft.setRotation(3);       // landscape
  tft.invertDisplay(true);  // Fix color inversion

  tft.fillScreen(C_BG);

  // touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touchSPI.begin(25, 39, 32, TOUCH_CS);  // CLK, MISO, MOSI, CS
  touch.begin(touchSPI);
  touch.setRotation(3);

  // initSD();  //Conflict with XPTtouch
  initFS();
  loadLog();


  drawScanScreen();

  Serial.println("CYD MAC Finder ready.");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  if (currentScreen == SCREEN_SCAN && scanState == STATE_SCANNING) {
    processScanResults();
    drawSpinner();
  }

  TouchPoint tp = getTouch();
  if (tp.pressed && !wasTouched) {
    wasTouched = true;
    if (currentScreen == SCREEN_SCAN) handleScanTouch(tp.x, tp.y);
    else if (currentScreen == SCREEN_LOG) handleLogTouch(tp.x, tp.y);
  } else if (!tp.pressed) {
    wasTouched = false;
  }

  delay(20);
}
