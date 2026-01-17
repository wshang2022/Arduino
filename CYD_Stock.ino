/*
 * ESP32-2432S028R Compact Stock Dashboard
 * All stocks displayed on one screen with trend graphs
 * Using Finnhub API with SD card persistence
 * 
 * Required Libraries:
 * - TFT_eSPI (configured for ILI9341)
 * - ArduinoJson
 * - WiFi (built-in)
 * - HTTPClient (built-in)
 * - SD (built-in)
 * - SPI (built-in)
 */

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// WiFi credentials
String ssid = "";
String password = "";

// Finnhub API key
String apiKey = "";

// Stock data structure
struct StockData {
  String symbol;
  float currentPrice;
  float previousPrice;
  float priceHistory[10];
  int historyCount;
};

// Stock list - will be populated from config.ini
#define MAX_STOCKS 10
StockData stocks[MAX_STOCKS];
int TOTAL_STOCKS = 0;

// Display settings
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define MAX_POINTS 10

// Colors
#define BG_COLOR 0x0000    // Black
#define TEXT_WHITE 0xFFFF  // White
#define UP_COLOR 0x07E0    // Green
#define DOWN_COLOR 0xF800  // Red
#define GRID_COLOR 0x2124  // Very dark gray

// Timing
unsigned long lastUpdate = 0;
unsigned long lastFlash = 0;
unsigned int rotation=1;
bool flashState = false;
const unsigned long UPDATE_INTERVAL = 60000;  // Update every 60 seconds
const unsigned long FLASH_INTERVAL = 500;     // Flash every 500ms

// SD Card
#define SD_CS 5
const char *CSV_FILE = "/stock.csv";
const char *CONFIG_FILE = "/config.ini";

void setup() {
  Serial.begin(115200);

  // Enable backlight
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  // Initialize display
  tft.init();
  tft.setRotation(rotation);       // Landscape
  tft.invertDisplay(true);  // Fix color inversion
  tft.fillScreen(BG_COLOR);

  // Create sprite for graphs (70x35 pixels each)
  sprite.createSprite(70, 35);

  // Initialize SD card
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card initialization failed!");
    tft.setTextSize(1);
    tft.setTextColor(DOWN_COLOR, BG_COLOR);
    tft.setCursor(100, 115);
    tft.print("SD Card Error!");
    delay(2000);
  } else {
    Serial.println("SD Card initialized");
    loadConfig();
    loadStockData();
  }

  // Connect to WiFi
  connectWiFi();

  // Initial display
  drawLoadingScreen();

  // Get initial stock data for all stocks
  updateAllStocks();

  // Save to SD card
  saveStockData();

  // Draw dashboard
  drawDashboard();
}

void loop() {
  // Handle flashing for price changes
  if (millis() - lastFlash > FLASH_INTERVAL) {
    flashState = !flashState;
    lastFlash = millis();
    drawDashboard();
  }

  // Update all stocks periodically
  if (millis() - lastUpdate > UPDATE_INTERVAL) {
    updateAllStocks();
    saveStockData();
    drawDashboard();
    lastUpdate = millis();
  }

  delay(3000);
}

void connectWiFi() {
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TEXT_WHITE, BG_COLOR);
  tft.setTextSize(1);
  tft.setCursor(100, 115);
  tft.print("Connecting WiFi...");

  if (ssid.length() == 0) {
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(DOWN_COLOR, BG_COLOR);
    tft.setCursor(80, 115);
    tft.print("No WiFi config found!");
    Serial.println("No WiFi credentials in config.ini");
    delay(5000);
    return;
  }

  WiFi.begin(ssid.c_str(), password.c_str());

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
}

void drawLoadingScreen() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_WHITE, BG_COLOR);
  tft.setCursor(110, 115);
  tft.print("Loading stocks...");
}

void updateAllStocks() {
  for (int i = 0; i < TOTAL_STOCKS; i++) {
    updateStockData(i);
    delay(500);  // Small delay between API calls
  }
}

void updateStockData(int index) {
  if (WiFi.status() != WL_CONNECTED || index >= TOTAL_STOCKS) {
    return;
  }

  StockData &stock = stocks[index];
  HTTPClient http;

  // Finnhub API endpoint
  String url = "https://finnhub.io/api/v1/quote?symbol=";
  url += stock.symbol;
  url += "&token=";
  url += apiKey;

  http.begin(url);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();

    // Parse JSON
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      float price = doc["c"];  // current price

      if (price > 0) {
        stock.previousPrice = stock.currentPrice;
        stock.currentPrice = price;

        // Add to history
        addToHistory(stock, price);

        Serial.print(stock.symbol);
        Serial.print(": $");
        Serial.println(stock.currentPrice, 2);
      }
    }
  } else {
    Serial.print("Error: ");
    Serial.println(stock.symbol);
  }

  http.end();
}

void addToHistory(StockData &stock, float price) {
  // Shift array left
  for (int i = 0; i < MAX_POINTS - 1; i++) {
    stock.priceHistory[i] = stock.priceHistory[i + 1];
  }

  // Add new price at end
  stock.priceHistory[MAX_POINTS - 1] = price;

  // Increment count if not full
  if (stock.historyCount < MAX_POINTS) {
    stock.historyCount++;
  }
}

void drawDashboard() {
  tft.fillScreen(BG_COLOR);

  // Draw title
  tft.setTextSize(1);
  tft.setTextColor(TEXT_WHITE, BG_COLOR);
  tft.setCursor(5, 5);
  tft.print("STOCK DASHBOARD");

  // Draw update time
  tft.setCursor(220, 5);
  tft.print("Updated: ");
  int sec = (millis() / 1000) % 60;
  int min = (millis() / 60000) % 60;
  if (min < 10) tft.print("0");
  tft.print(min);
  tft.print(":");
  if (sec < 10) tft.print("0");
  tft.print(sec);

  // Draw separator line
  tft.drawLine(0, 15, 320, 15, GRID_COLOR);

  // Calculate layout (2x2 grid for 4 stocks)
  int rows = 2;
  int cols = 2;
  int cellWidth = 160;
  int cellHeight = 112;
  int startY = 18;

  // Draw each stock
  for (int i = 0; i < TOTAL_STOCKS && i < rows * cols; i++) {
    int row = i / cols;
    int col = i % cols;
    int x = col * cellWidth;
    int y = startY + (row * cellHeight);

    drawStockCell(stocks[i], x, y, cellWidth, cellHeight);
  }
}

void drawStockCell(StockData &stock, int x, int y, int w, int h) {
  // Draw cell border
  tft.drawRect(x, y, w, h, GRID_COLOR);

  // Stock symbol
  tft.setTextSize(1);
  tft.setTextColor(TEXT_WHITE, BG_COLOR);
  tft.setCursor(x + 3, y + 3);
  tft.print(stock.symbol);

  // Determine price color with flashing
  uint16_t priceColor = TEXT_WHITE;
  bool shouldFlash = false;

  if (stock.previousPrice > 0) {
    if (stock.currentPrice > stock.previousPrice) {
      priceColor = UP_COLOR;
      shouldFlash = true;
    } else if (stock.currentPrice < stock.previousPrice) {
      priceColor = DOWN_COLOR;
      shouldFlash = true;
    }
  }

  // Apply flash effect
  if (shouldFlash && !flashState) {
    priceColor = BG_COLOR;  // Turn off text for flash effect
  }

  // Current price
  tft.setTextSize(2);
  tft.setTextColor(priceColor, BG_COLOR);
  tft.setCursor(x + 3, y + 14);
  tft.print("$");

  // Adjust font size based on price magnitude
  char priceStr[12];
  if (stock.currentPrice >= 1000) {
    dtostrf(stock.currentPrice, 0, 1, priceStr);
    tft.setTextSize(1);
    tft.setCursor(x + 15, y + 18);
  } else if (stock.currentPrice >= 100) {
    dtostrf(stock.currentPrice, 0, 2, priceStr);
    tft.setCursor(x + 15, y + 14);
  } else {
    dtostrf(stock.currentPrice, 0, 2, priceStr);
    tft.setCursor(x + 15, y + 14);
  }
  tft.print(priceStr);

  // Price change percentage
  if (stock.previousPrice > 0 && stock.currentPrice > 0) {
    float change = stock.currentPrice - stock.previousPrice;
    float changePercent = (change / stock.previousPrice) * 100.0;

    tft.setTextSize(1);
    tft.setCursor(x + 3, y + 34);

    uint16_t changeColor = TEXT_WHITE;
    if (change > 0) {
      changeColor = UP_COLOR;
      if (!flashState) changeColor = BG_COLOR;
      tft.setTextColor(changeColor, BG_COLOR);
      tft.print("+");
    } else if (change < 0) {
      changeColor = DOWN_COLOR;
      if (!flashState) changeColor = BG_COLOR;
      tft.setTextColor(changeColor, BG_COLOR);
    } else {
      tft.setTextColor(TEXT_WHITE, BG_COLOR);
    }

    tft.print(changePercent, 2);
    tft.print("%");
  }

  // Draw graph
  drawMiniGraph(stock, x + 5, y + 47, 150, 60);
}

void drawMiniGraph(StockData &stock, int x, int y, int w, int h) {
  if (stock.historyCount < 2) {
    tft.setTextSize(1);
    tft.setTextColor(GRID_COLOR, BG_COLOR);
    tft.setCursor(x + 20, y + 25);
    tft.print("No data");
    return;
  }

  // Find min and max
  float minPrice = stock.priceHistory[0];
  float maxPrice = stock.priceHistory[0];

  for (int i = 0; i < stock.historyCount; i++) {
    if (stock.priceHistory[i] < minPrice) minPrice = stock.priceHistory[i];
    if (stock.priceHistory[i] > maxPrice) maxPrice = stock.priceHistory[i];
  }

  // Add padding
  float range = maxPrice - minPrice;
  if (range < 0.01) range = stock.currentPrice * 0.02;
  minPrice -= range * 0.1;
  maxPrice += range * 0.1;
  range = maxPrice - minPrice;

  // Draw grid
  tft.drawLine(x, y, x + w, y, GRID_COLOR);
  tft.drawLine(x, y + h / 2, x + w, y + h / 2, GRID_COLOR);
  tft.drawLine(x, y + h, x + w, y + h, GRID_COLOR);

  // Calculate spacing
  float xSpacing = (float)w / (stock.historyCount - 1);

  // Determine line color based on overall trend
  uint16_t lineColor = TEXT_WHITE;
  if (stock.currentPrice > stock.priceHistory[0]) {
    lineColor = UP_COLOR;
  } else if (stock.currentPrice < stock.priceHistory[0]) {
    lineColor = DOWN_COLOR;
  }

  // Draw line graph
  for (int i = 0; i < stock.historyCount - 1; i++) {
    int x1 = x + (i * xSpacing);
    int y1 = y + h - ((stock.priceHistory[i] - minPrice) / range * h);

    int x2 = x + ((i + 1) * xSpacing);
    int y2 = y + h - ((stock.priceHistory[i + 1] - minPrice) / range * h);

    tft.drawLine(x1, y1, x2, y2, lineColor);
  }

  // Draw min/max labels
  tft.setTextSize(1);
  tft.setTextColor(GRID_COLOR, BG_COLOR);

  tft.setCursor(x + w - 35, y);
  tft.print("$");
  tft.print(maxPrice, 1);

  tft.setCursor(x + w - 35, y + h - 7);
  tft.print("$");
  tft.print(minPrice, 1);
}

void saveStockData() {
  // Remove old file
  if (SD.exists(CSV_FILE)) {
    SD.remove(CSV_FILE);
  }

  File file = SD.open(CSV_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  // Write each stock
  for (int i = 0; i < TOTAL_STOCKS; i++) {
    StockData &stock = stocks[i];
    file.print(stock.symbol);

    for (int j = 0; j < MAX_POINTS; j++) {
      file.print(",");
      file.print(stock.priceHistory[j], 2);
    }

    file.println();
  }

  file.close();
  Serial.println("Stock data saved to SD card");
}

void loadStockData() {
  File file = SD.open(CSV_FILE);
  if (!file) {
    Serial.println("No existing stock data file");
    return;
  }

  int stockIndex = 0;
  while (file.available() && stockIndex < TOTAL_STOCKS) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (line.length() > 0) {
      // Parse CSV line
      int commaIndex = 0;
      int lastIndex = 0;
      String symbol = "";
      int valueIndex = 0;

      for (int i = 0; i <= line.length(); i++) {
        if (i == line.length() || line.charAt(i) == ',') {
          String value = line.substring(lastIndex, i);

          if (valueIndex == 0) {
            symbol = value;
            symbol.trim();
          } else {
            float price = value.toFloat();
            if (valueIndex - 1 < MAX_POINTS) {
              stocks[stockIndex].priceHistory[valueIndex - 1] = price;
              if (price > 0) {
                stocks[stockIndex].historyCount = valueIndex;
                stocks[stockIndex].currentPrice = price;
              }
            }
          }

          lastIndex = i + 1;
          valueIndex++;
        }
      }

      // Verify symbol matches
      if (symbol.equals(stocks[stockIndex].symbol)) {
        Serial.print("Loaded data for ");
        Serial.println(symbol);
      } else {
        Serial.print("Warning: CSV symbol ");
        Serial.print(symbol);
        Serial.print(" doesn't match config symbol ");
        Serial.println(stocks[stockIndex].symbol);
      }

      stockIndex++;
    }
  }

  file.close();
  Serial.println("Stock data loaded from SD card");
}

void loadConfig() {
  File file = SD.open(CONFIG_FILE);
  if (!file) {
    Serial.println("Config file not found!");
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(DOWN_COLOR, BG_COLOR);
    tft.setTextSize(1);
    tft.setCursor(70, 100);
    tft.print("config.ini not found!");
    tft.setCursor(70, 115);
    tft.print("Create config.ini on SD:");
    tft.setCursor(70, 130);
    tft.print("ssid=YOUR_WIFI");
    tft.setCursor(70, 145);
    tft.print("password=YOUR_PASSWORD");
    tft.setCursor(70, 160);
    tft.print("apikey=YOUR_APIKEY");
    tft.setCursor(70, 175);
    tft.print("stocks=IVV,GLD,COPX,NVDA");
    tft.setCursor(70, 190);
    tft.print("rotation=1");
    delay(10000);
    return;
  }

  Serial.println("Loading config.ini...");

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    // Skip empty lines and comments
    if (line.length() == 0 || line.startsWith("#") || line.startsWith(";")) {
      continue;
    }

    // Find the = separator
    int separatorIndex = line.indexOf('=');
    if (separatorIndex == -1) {
      continue;
    }

    // Extract key and value
    String key = line.substring(0, separatorIndex);
    String value = line.substring(separatorIndex + 1);

    // Trim whitespace
    key.trim();
    value.trim();

    // Remove quotes if present
    if (value.startsWith("\"") && value.endsWith("\"")) {
      value = value.substring(1, value.length() - 1);
    }
    if (value.startsWith("'") && value.endsWith("'")) {
      value = value.substring(1, value.length() - 1);
    }

    // Assign to variables
    if (key.equalsIgnoreCase("ssid")) {
      ssid = value;
      Serial.print("SSID: ");
      Serial.println(ssid);
    } else if (key.equalsIgnoreCase("password")) {
      password = value;
      Serial.println("Password: ****");
    } else if (key.equalsIgnoreCase("apikey")) {
      apiKey = value;
      Serial.print("API Key: ");
      Serial.println(apiKey.substring(0, 8) + "...");
    } else if (key.equalsIgnoreCase("stocks")) {
      parseStocks(value);
    } else if (key.equalsIgnoreCase("rotation")) {
      rotation = (unsigned int)value.toInt();;
      Serial.print("rotation: ");
      Serial.println(rotation);
    }
  }

  file.close();
  Serial.println("Config loaded successfully");

  // If no stocks configured, use defaults
  if (TOTAL_STOCKS == 0) {
    Serial.println("No stocks configured, using defaults");
    parseStocks("IVV,GLD,COPX,NVDA");
  }
}

void parseStocks(String stockList) {
  TOTAL_STOCKS = 0;
  int startIndex = 0;

  stockList.trim();

  for (int i = 0; i <= stockList.length(); i++) {
    if (i == stockList.length() || stockList.charAt(i) == ',') {
      if (i > startIndex && TOTAL_STOCKS < MAX_STOCKS) {
        String symbol = stockList.substring(startIndex, i);
        symbol.trim();
        symbol.toUpperCase();

        if (symbol.length() > 0) {
          stocks[TOTAL_STOCKS].symbol = symbol;
          stocks[TOTAL_STOCKS].currentPrice = 0;
          stocks[TOTAL_STOCKS].previousPrice = 0;
          stocks[TOTAL_STOCKS].historyCount = 0;
          for (int j = 0; j < MAX_POINTS; j++) {
            stocks[TOTAL_STOCKS].priceHistory[j] = 0;
          }

          Serial.print("Stock ");
          Serial.print(TOTAL_STOCKS + 1);
          Serial.print(": ");
          Serial.println(symbol);

          TOTAL_STOCKS++;
        }
      }
      startIndex = i + 1;
    }
  }

  Serial.print("Total stocks configured: ");
  Serial.println(TOTAL_STOCKS);
}