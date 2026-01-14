// LILYGO TTGO T-Display (ESP32)
// Only work esp32 version 2.0.17 or 2.0.7 DO NOT USE > 3.x
// Tool -> Board -> ESP32 Dev Module (or TTGO T1)
// Tools -> Upload Speed -> 921600
// Partition Scheme -> Default 4MB
// ESP32-D0WDQ6-V3
// Built-in 1.14" IPS LCD
// Resolution 135 x 240
// Driver ST7789
// Backlight pin TFT_BL = 4
// Buttons GPIO 0 and GPIO 35
// USB-to-UART: CP2102 / CP210x
// Tool -> Board -> ESP32 Dev Module (or TTGO T1)
// Tools -> Upload Speed -> 921600
// Partition Scheme -> Default 4MB
// "Fold All" is Ctrl+K Ctrl+0
// It is NOT CH9102F / CH340C
#define USER_SETUP_LOADED
#include "TFT_eSPI_Setup.h"
#include <TFT_eSPI.h>

#include <Arduino.h>
#include "esp32-hal-ledc.h"
#include <FS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "time.h"  // Native ESP32 time library

// ================= CONFIGURATION =================
const char* FINNHUB_KEY = "XXXXX";
const char* COINGECKO_URL = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum,solana&vs_currencies=usd";

// Default Wi-Fi credentials
const String defaultSSID = "SSID";
const String defaultPassword = "";

// === ADD THESE WITH YOUR OTHER GLOBALS ===
unsigned long rotatePressStart = 0;
bool rotateButtonHeld = false;
int lastRotateButtonState = HIGH;

// Timezone Setup for New York (EST/EDT)
// EST5EDT = Standard time is EST (+5), Daylight Saving is EDT.
// M3.2.0 = DST starts March (3), 2nd week (2), Sunday (0)
// M11.1.0 = DST ends November (11), 1st week (1), Sunday (0)
const char* ntpServer = "pool.ntp.org";
const char* timeZone = "EST5EDT,M3.2.0,M11.1.0";

// Pins
#define TFT_BL 4
#define LED_PIN 22
const int buttonPin = 35;
const int rotateButtonPin = 0;

// ================= GLOBALS =================
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

WebServer server(80);

// Screen State
int screenRotation = 1;

// Brightness
int brightness[] = { 40, 80, 120, 160, 200 };
int brightnessIndex = 1;
const int pwmLedChannelTFT = 0;

// Data Storage
struct StockData {
  const char* symbol;
  float price;
  float prevPrice;
};

StockData stocks[] = {
  { "IVV", -1, -1 },
  { "GLD", -1, -1 },
  { "COPX", -1, -1 },
  { "NVDA", -1, -1 }
};
const int TOTAL_STOCKS = 4;

float btcPrice = -1, prevBtcPrice = -1;
float ethPrice = -1, prevEthPrice = -1;
float solPrice = -1, prevSolPrice = -1;

// Button State
unsigned long buttonPressStart = 0;
bool buttonHeld = false;
int lastButtonState = HIGH;

// Override State (User requested to see the OTHER screen temporarily)
bool overrideActive = false;
unsigned long overrideStartTime = 0;
const unsigned long overrideDuration = 15000;  // 15 seconds

// INTERRUPT FLAG for Rotation
volatile bool rotateRequested = false;
unsigned long lastRotateTime = 0;

// API Schedulers
unsigned long lastStockUpdate = 0;
int currentStockIndex = 0;
const unsigned long STOCK_UPDATE_INTERVAL = 5000;  // 5s per stock

unsigned long lastCryptoUpdate = 0;
const unsigned long CRYPTO_UPDATE_INTERVAL = 30000;  // 30s for crypto (CoinGecko limit)

unsigned long lastWifiCheck = 0;
bool timeSynced = false;

// ================= HELPERS =================

// void IRAM_ATTR handleRotationInterrupt() {
//   rotateRequested = true;
// }

void blinkLED() {
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

// EEPROM
void writeStringToEEPROM(int address, String value) {
  int length = value.length();
  if (length > 99) length = 99;
  EEPROM.write(address, length);
  for (int i = 0; i < length; i++) EEPROM.write(address + 1 + i, value[i]);
  EEPROM.commit();
}

String readStringFromEEPROM(int address) {
  int length = EEPROM.read(address);
  if (length == 255) return "";
  String value = "";
  for (int i = 0; i < length; i++) value += char(EEPROM.read(address + 1 + i));
  return value;
}

void saveRotationToEEPROM(int rot) {
  EEPROM.write(300, rot);
  EEPROM.commit();
}

int loadRotationFromEEPROM() {
  byte rot = EEPROM.read(300);
  return (rot > 3) ? 1 : rot;
}

// ================= TIME LOGIC =================

// Check if we are in market hours (Mon-Fri, 9:30 - 16:30 EST)
bool isMarketOpen() {
  return true;
  if (!timeSynced) return false;  // Default to Crypto if time not set

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return false;
  }

  // 0 = Sunday, 6 = Saturday
  if (timeinfo.tm_wday == 0 || timeinfo.tm_wday == 6) return false;

  int hour = timeinfo.tm_hour;
  int min = timeinfo.tm_min;

  // Market opens 9:30
  if (hour < 9) return false;
  if (hour == 9 && min < 30) return false;

  // Market closes 16:30 (4:30 PM)
  if (hour > 16) return false;
  if (hour == 16 && min >= 30) return false;

  return true;
}

String getClockString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "--:--";
  }
  char timeStringBuff[10];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
  return String(timeStringBuff);
}

// ================= DRAWING =================

uint16_t getPriceColor(float current, float prev) {
  if (prev < 0) return TFT_WHITE;
  if (current > prev) return TFT_GREEN;
  if (current < prev) return TFT_RED;
  return TFT_WHITE;
}

void drawStockScreen() {
  sprite.fillSprite(TFT_BLACK);
  sprite.setTextDatum(TL_DATUM);

  int y = 10;
  int rowHeight = 30;

  for (int i = 0; i < TOTAL_STOCKS; i++) {
    sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    sprite.drawString(stocks[i].symbol, 10, y);

    sprite.setTextColor(getPriceColor(stocks[i].price, stocks[i].prevPrice), TFT_BLACK);
    sprite.setTextDatum(TR_DATUM);

    if (stocks[i].price > 0) {
      sprite.drawFloat(stocks[i].price, 2, sprite.width() - 10, y);
    } else {
      sprite.drawString("...", sprite.width() - 10, y);
    }
    sprite.setTextDatum(TL_DATUM);
    y += rowHeight;
  }

  // Draw Clock in corner
  sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sprite.setTextDatum(BR_DATUM);
  sprite.drawString(getClockString(), sprite.width() - 5, sprite.height() - 5);

  sprite.pushSprite(0, 0);
}

void drawCryptoScreen() {
  sprite.fillSprite(TFT_BLACK);
  int y = 20;
  sprite.setTextDatum(TL_DATUM);

  sprite.setTextColor(TFT_ORANGE, TFT_BLACK);
  sprite.drawString("BTC", 10, y);
  sprite.setTextColor(getPriceColor(btcPrice, prevBtcPrice), TFT_BLACK);
  sprite.drawFloat(btcPrice, 2, 70, y);

  y += 40;
  sprite.setTextColor(TFT_CYAN, TFT_BLACK);
  sprite.drawString("ETH", 10, y);
  sprite.setTextColor(getPriceColor(ethPrice, prevEthPrice), TFT_BLACK);
  sprite.drawFloat(ethPrice, 2, 70, y);

  y += 40;
  sprite.setTextColor(TFT_MAGENTA, TFT_BLACK);
  sprite.drawString("SOL", 10, y);
  sprite.setTextColor(getPriceColor(solPrice, prevSolPrice), TFT_BLACK);
  sprite.drawFloat(solPrice, 2, 70, y);

  // Draw Clock in corner
  sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sprite.setTextDatum(BR_DATUM);
  sprite.drawString(getClockString(), sprite.width() - 5, sprite.height() - 5);

  sprite.pushSprite(0, 0);
}

// ================= API FETCHING =================

void ensureWiFi() {
  if (millis() - lastWifiCheck > 10000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      String ssid = readStringFromEEPROM(0);
      String pass = readStringFromEEPROM(100);
      if (ssid == "") {
        ssid = defaultSSID;
        pass = defaultPassword;
      }
      WiFi.begin(ssid.c_str(), pass.c_str());
    }
  }
}

void fetchFinnhub(int stockIndex) {
  if (WiFi.status() != WL_CONNECTED) return;

  String symbol = stocks[stockIndex].symbol;
  String url = "https://finnhub.io/api/v1/quote?symbol=" + symbol + "&token=" + String(FINNHUB_KEY);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  if (!http.begin(client, url)) return;

  int code = http.GET();
  if (code == 200) {
    String json = http.getString();
    StaticJsonDocument<128> filter;
    filter["c"] = true;
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, json, DeserializationOption::Filter(filter));

    if (doc.containsKey("c")) {
      float newPrice = doc["c"].as<float>();
      if (abs(newPrice - stocks[stockIndex].price) > 0.001) blinkLED();
      stocks[stockIndex].prevPrice = stocks[stockIndex].price;
      stocks[stockIndex].price = newPrice;
    }
  }
  http.end();
}

void updateCryptoPrices() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, COINGECKO_URL);

  if (http.GET() == 200) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, http.getString());

    float newBtc = doc["bitcoin"]["usd"];
    float newEth = doc["ethereum"]["usd"];
    float newSol = doc["solana"]["usd"];

    if (abs(newBtc - btcPrice) > 0.01 || abs(newEth - ethPrice) > 0.01) blinkLED();

    prevBtcPrice = btcPrice;
    btcPrice = newBtc;
    prevEthPrice = ethPrice;
    ethPrice = newEth;
    prevSolPrice = solPrice;
    solPrice = newSol;
  }
  http.end();
}

// ================= WEB SERVER =================
void handleRoot() {
  String page = "<html><body><h1>Wi-Fi Config</h1><form action='/save' method='post'>";
  page += "SSID: <input type='text' name='ssid'><br>";
  page += "Pass: <input type='password' name='password'><br>";
  page += "<input type='submit' value='Save'></form></body></html>";
  server.send(200, "text/html", page);
}

void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  if (ssid.length() > 0) {
    writeStringToEEPROM(0, ssid);
    writeStringToEEPROM(100, password);
    server.send(200, "text/html", "Saved. Rebooting...");
    delay(1000);
    ESP.restart();
  }
}

// ================= SETUP & LOOP =================
void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);

  tft.init();
  screenRotation = loadRotationFromEEPROM();
  tft.setRotation(screenRotation);
  if (screenRotation % 2 == 1) sprite.createSprite(240, 135);
  else sprite.createSprite(135, 240);
  sprite.setTextSize(2);

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(rotateButtonPin, INPUT_PULLUP);
  // attachInterrupt(digitalPinToInterrupt(rotateButtonPin), handleRotationInterrupt, FALLING);

  ledcSetup(pwmLedChannelTFT, 5000, 8);
  ledcAttachPin(TFT_BL, pwmLedChannelTFT);
  ledcWrite(pwmLedChannelTFT, brightness[brightnessIndex]);

  // WiFi Setup
  String ssid = readStringFromEEPROM(0);
  String pass = readStringFromEEPROM(100);
  if (ssid == "") {
    ssid = defaultSSID;
    pass = defaultPassword;
  }

  WiFi.begin(ssid.c_str(), pass.c_str());
  sprite.fillSprite(TFT_BLACK);
  sprite.setTextColor(TFT_WHITE);
  sprite.drawString("Connecting...", 10, 10);
  sprite.pushSprite(0, 0);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 15) {
    delay(500);
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // --- INIT TIME ---
    configTime(0, 0, ntpServer);
    setenv("TZ", timeZone, 1);
    tzset();

    sprite.fillSprite(TFT_BLACK);
    sprite.drawString("Syncing Time...", 10, 10);
    sprite.pushSprite(0, 0);

    // Wait for time sync (up to 5 sec)
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) {
      delay(500);
      retry++;
    }
    if (retry < 10) timeSynced = true;

    // Initial Fetch of both to populate data
    updateCryptoPrices();
    fetchFinnhub(0);
  } else {
    WiFi.softAP("ESP32-TICKER");
    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.begin();
    sprite.fillSprite(TFT_BLACK);
    sprite.drawString("AP Mode:", 10, 10);
    sprite.pushSprite(0, 0);
  }
}

void loop() {
  server.handleClient();
  ensureWiFi();
  unsigned long now = millis();

  int rotateVal = digitalRead(rotateButtonPin);

  // Detect press start
  if (rotateVal == LOW && lastRotateButtonState == HIGH) {
    rotatePressStart = now;
    rotateButtonHeld = false;
  }

  // Long press detected → force Stock screen (override)
  if (rotateVal == LOW && !rotateButtonHeld && (now - rotatePressStart > 1000)) {
    rotateButtonHeld = true;
    overrideActive = true;
    overrideStartTime = now;
    // Optional: small feedback
    blinkLED();
  }

  // Short press released → rotate screen (your original behavior)
  if (rotateVal == HIGH && lastRotateButtonState == LOW && !rotateButtonHeld) {
    if (now - lastRotateTime > 250) {
      screenRotation++;
      if (screenRotation > 3) screenRotation = 0;
      tft.setRotation(screenRotation);
      saveRotationToEEPROM(screenRotation);
      sprite.deleteSprite();
      if (screenRotation % 2 == 1) sprite.createSprite(240, 135);
      else sprite.createSprite(135, 240);
      sprite.setTextSize(2);
      lastRotateTime = now;
      blinkLED();  // feedback
    }
  }

  lastRotateButtonState = rotateVal;

  // Clear interrupt flag (in case it fired during press)
  rotateRequested = false;

  // --- 2. HANDLE BUTTON (LONG PRESS = OVERRIDE) ---
  int btnVal = digitalRead(buttonPin);

  if (btnVal == LOW && lastButtonState == HIGH) {
    buttonPressStart = now;
    buttonHeld = false;
  }

  if (btnVal == LOW && !buttonHeld && (now - buttonPressStart > 1000)) {
    buttonHeld = true;
    overrideActive = true;
    overrideStartTime = now;
    if (isMarketOpen()) fetchFinnhub(currentStockIndex);
    else updateCryptoPrices();
  }


  if (btnVal == HIGH && lastButtonState == LOW && !buttonHeld) {
    brightnessIndex = (brightnessIndex + 1) % 5;
    ledcWrite(pwmLedChannelTFT, brightness[brightnessIndex]);
  }
  lastButtonState = btnVal;

  // --- 3. DETERMINE WHAT TO SHOW ---

  // Timeout for override
  if (overrideActive && (now - overrideStartTime > overrideDuration)) {
    overrideActive = false;
  }

  bool marketOpen = isMarketOpen();

  // Logic: Show Stocks if (Market Open AND No Override) OR (Market Closed AND Override)
  bool showStocks = (marketOpen && !overrideActive) || (!marketOpen && overrideActive);

  // --- 4. UPDATE DATA & DRAW ---

  if (showStocks) {
    // We are viewing stocks
    if (WiFi.status() == WL_CONNECTED && now - lastStockUpdate > STOCK_UPDATE_INTERVAL) {
      lastStockUpdate = now;
      fetchFinnhub(currentStockIndex);
      currentStockIndex++;
      if (currentStockIndex >= TOTAL_STOCKS) currentStockIndex = 0;
    }
    // Always redraw (to update clock)
    drawStockScreen();
  } else {
    // We are viewing Crypto
    if (WiFi.status() == WL_CONNECTED && now - lastCryptoUpdate > CRYPTO_UPDATE_INTERVAL) {
      lastCryptoUpdate = now;
      updateCryptoPrices();
    }
    drawCryptoScreen();
  }

  // Slow down the loop slightly to prevent screen tearing/CPU hogging
  delay(100);
}
