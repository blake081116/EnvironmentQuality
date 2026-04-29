#include <math.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <bsec2.h>
#include <Preferences.h>
#include <SensirionI2cSps30.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const int IEQ_SCORE = 100;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;
const int OLED_ADDRESS = 0x3C;

const int BUTTON_PIN = 6;         // Noi nut vao D6 va GND
const int PAGE_COUNT = 6;
const uint32_t BUTTON_DEBOUNCE_MS = 50;
const uint32_t DISPLAY_UPDATE_MS = 250;
const uint32_t POPUP_DURATION_MS = 1600;
const uint32_t SERIAL_BAUD = 115200;
const uint32_t SERIAL_SEND_MS = 1000;
const uint32_t HTTP_SEND_MS = 1000;
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 12000;

// Dien Wi-Fi va IP may dang chay FastAPI de board gui data qua mang.
// Chay laptop server bang: uvicorn dashboard.app:app --host 0.0.0.0 --port 8000
// Lay IP laptop bang: ipconfig getifaddr en0
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";
const char* API_URL = "";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

extern uint8_t latestIaqAccuracy;
extern float latestIaq;
extern float latestStaticIaq;
extern float latestEco2;
extern float latestBvoc;
extern float latestRawTemp;
extern float latestTemp;
extern float latestRawHum;
extern float latestHum;
extern float latestPress;
extern float latestRawGas;
extern float latestCompGas;
extern float latestGasPercentage;
extern float latestStabilizationStatus;
extern float latestDb;
extern float latestVrms;
extern float latestPm1p0;
extern float latestPm2p5;
extern float latestPm4p0;
extern float latestPm10p0;
extern float latestNc0p5;
extern float latestNc1p0;
extern float latestNc2p5;
extern float latestNc4p0;
extern float latestNc10p0;
extern float latestTypicalParticleSize;
extern bool latestSps30Ready;
extern int16_t latestSps30Error;

void initBME688();
void updateBME688();
void initMax4466();
void updateMax4466();
void initSPS30();
void updateSPS30();

bool displayReady = false;
uint8_t currentPage = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastSerialMs = 0;
uint32_t lastHttpMs = 0;

bool buttonLastReading = HIGH;
bool buttonStableState = HIGH;
uint32_t lastButtonChangeMs = 0;

const char *popupLine1 = nullptr;
const char *popupLine2 = nullptr;
uint32_t popupUntilMs = 0;

void drawCenteredText(const char *text, int y, uint8_t size)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(text);
}

void showBootMessage(const char *line1, const char *line2)
{
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText(line1, 12, 1);
  drawCenteredText(line2, 32, 1);
  display.display();
}

void showPopup(const char *line1, const char *line2)
{
  popupLine1 = line1;
  popupLine2 = line2;
  popupUntilMs = millis() + POPUP_DURATION_MS;
}

void drawPopup()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  drawCenteredText(popupLine1 ? popupLine1 : "Notice", 16, 1);
  drawCenteredText(popupLine2 ? popupLine2 : "", 34, 1);
  display.display();
}

void drawValue(float value, uint8_t digits)
{
  if (isnan(value)) {
    display.print("--");
  } else {
    display.print(value, digits);
  }
}

String jsonFloat(float value, uint8_t digits)
{
  if (isnan(value) || isinf(value)) {
    return "null";
  }

  return String(value, (unsigned int)digits);
}

String makeJsonSample()
{
  String json = "{\"ieq\":{\"score\":";
  json += IEQ_SCORE;
  json += "},\"bme\":{\"temp\":";
  json += jsonFloat(latestTemp, 2);
  json += ",\"rawTemp\":";
  json += jsonFloat(latestRawTemp, 2);
  json += ",\"humidity\":";
  json += jsonFloat(latestHum, 2);
  json += ",\"rawHumidity\":";
  json += jsonFloat(latestRawHum, 2);
  json += ",\"pressure\":";
  json += jsonFloat(latestPress, 2);
  json += ",\"iaq\":";
  json += jsonFloat(latestIaq, 1);
  json += ",\"staticIaq\":";
  json += jsonFloat(latestStaticIaq, 1);
  json += ",\"accuracy\":";
  json += String(latestIaqAccuracy);
  json += ",\"eco2\":";
  json += jsonFloat(latestEco2, 1);
  json += ",\"bvoc\":";
  json += jsonFloat(latestBvoc, 3);
  json += ",\"gas\":";
  json += jsonFloat(latestCompGas, 0);
  json += ",\"rawGas\":";
  json += jsonFloat(latestRawGas, 0);
  json += ",\"compGas\":";
  json += jsonFloat(latestCompGas, 0);
  json += ",\"gasPercentage\":";
  json += jsonFloat(latestGasPercentage, 1);
  json += ",\"stabilizationStatus\":";
  json += jsonFloat(latestStabilizationStatus, 0);
  json += "},\"sound\":{\"db\":";
  json += jsonFloat(latestDb, 1);
  json += ",\"vrms\":";
  json += jsonFloat(latestVrms, 4);
  json += "},\"sps30\":{\"ready\":";
  json += latestSps30Ready ? "true" : "false";
  json += ",\"error\":";
  json += String(latestSps30Error);
  json += ",\"pm1p0\":";
  json += jsonFloat(latestPm1p0, 1);
  json += ",\"pm2p5\":";
  json += jsonFloat(latestPm2p5, 1);
  json += ",\"pm4p0\":";
  json += jsonFloat(latestPm4p0, 1);
  json += ",\"pm10p0\":";
  json += jsonFloat(latestPm10p0, 1);
  json += ",\"nc0p5\":";
  json += jsonFloat(latestNc0p5, 1);
  json += ",\"nc1p0\":";
  json += jsonFloat(latestNc1p0, 1);
  json += ",\"nc2p5\":";
  json += jsonFloat(latestNc2p5, 1);
  json += ",\"nc4p0\":";
  json += jsonFloat(latestNc4p0, 1);
  json += ",\"nc10p0\":";
  json += jsonFloat(latestNc10p0, 1);
  json += ",\"typicalSize\":";
  json += jsonFloat(latestTypicalParticleSize, 2);
  json += "}}";
  return json;
}

void sendSerialSample()
{
  Serial.println(makeJsonSample());
}

bool wifiCredentialsConfigured()
{
  return WIFI_SSID[0] != '\0';
}

bool httpPushConfigured()
{
  return API_URL[0] != '\0';
}

void connectWiFi()
{
  if (!wifiCredentialsConfigured()) {
    showBootMessage("WiFi skipped", "Set WIFI_SSID");
    Serial.println("WiFi skipped: set WIFI_SSID/WIFI_PASSWORD to enable wireless mode.");
    delay(900);
    return;
  }

  showBootMessage("WiFi", "Connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    showBootMessage("WiFi connected", WiFi.localIP().toString().c_str());
    Serial.print("WiFi connected: ");
    Serial.print(WiFi.SSID());
    Serial.print(" ");
    Serial.println(WiFi.localIP());
    delay(900);
  } else {
    showBootMessage("WiFi failed", "Check SSID/pass");
    Serial.println("WiFi failed: check WIFI_SSID/WIFI_PASSWORD.");
    delay(1200);
  }
}

void sendHttpSample()
{
  if (!wifiCredentialsConfigured() || !httpPushConfigured()) return;

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    return;
  }

  HTTPClient http;
  http.setTimeout(1500);
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  http.POST(makeJsonSample());
  http.end();
}

String displayTextLimit(String text, uint8_t maxChars)
{
  if (text.length() <= maxChars) return text;

  return text.substring(0, maxChars);
}

String pageWiFiName()
{
  if (WiFi.status() == WL_CONNECTED) {
    String ssid = WiFi.SSID();
    if (ssid.length()) return ssid;
  }

  return "--";
}

String connectionStatusText()
{
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  if (Serial) return "Serial";
#endif

  return "Power only";
}

void drawPageInfo()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("Page 0: Info");

  display.setCursor(0, 10);
  display.print("WiFi: ");
  display.print(displayTextLimit(pageWiFiName(), 15));

  display.setCursor(0, 22);
  display.print("Status: ");
  display.print(connectionStatusText());

  display.setCursor(0, 34);
  display.print("IEQ SCORE");

  display.setTextSize(2);
  display.setCursor(0, 46);
  display.print(IEQ_SCORE);
  display.print("/100");

  display.display();
}

void drawPageBMEFirst()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("Page 1: BME688");

  display.setCursor(0, 9);
  display.print("IAQ: ");
  drawValue(latestIaq, 1);

  display.setCursor(0, 18);
  display.print("Static: ");
  drawValue(latestStaticIaq, 1);

  display.setCursor(0, 27);
  display.print("Raw T: ");
  drawValue(latestRawTemp, 1);
  display.print(" C");

  display.setCursor(0, 36);
  display.print("Comp T:");
  drawValue(latestTemp, 1);
  display.print(" C");

  display.setCursor(0, 45);
  display.print("Raw H: ");
  drawValue(latestRawHum, 1);
  display.print(" %");

  display.setCursor(0, 54);
  display.print("Comp H:");
  drawValue(latestHum, 1);
  display.print(" %");

  display.display();
}

void drawPageBMESecond()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("Page 2: BME688");

  display.setCursor(0, 14);
  display.print("eCO2: ");
  drawValue(latestEco2, 0);
  display.print(" ppm");

  display.setCursor(0, 28);
  display.print("bVOC: ");
  drawValue(latestBvoc, 3);
  display.print(" ppm");

  display.setCursor(0, 42);
  display.print("RawGas: ");
  drawValue(latestRawGas, 0);
  display.print(" ohm");

  display.display();
}

void drawPageBMEThird()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("Page 3: BME688");

  display.setCursor(0, 10);
  display.print("Acc: ");
  display.print(latestIaqAccuracy);
  display.print("/3");

  display.setCursor(0, 22);
  display.print("Stab: ");
  drawValue(latestStabilizationStatus, 0);

  display.setCursor(0, 34);
  display.print("CompGas: ");
  drawValue(latestCompGas, 0);
  display.print(" ohm");

  display.setCursor(0, 48);
  display.print("Gas %: ");
  drawValue(latestGasPercentage, 1);
  display.print(" %");

  display.display();
}

void drawPageSound()
{
  int barWidth = 0;
  if (!isnan(latestDb)) {
    barWidth = map(constrain((int)latestDb, 30, 100), 30, 100, 0, 124);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Page 4: Sound");

  display.setTextSize(2);
  display.setCursor(0, 16);
  drawValue(latestDb, 1);
  display.print(" dB");

  display.setTextSize(1);
  display.setCursor(0, 42);
  display.print("Vrms: ");
  drawValue(latestVrms, 4);
  display.print(" V");

  display.drawRect(0, 54, 128, 10, SSD1306_WHITE);
  display.fillRect(2, 56, barWidth, 6, SSD1306_WHITE);

  display.display();
}

void drawPageSPS30()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("Page 5: SPS30");

  if (!latestSps30Ready) {
    display.setCursor(0, 18);
    display.print("Sensor not ready");
    display.setCursor(0, 30);
    display.print("Check 5V/I2C/SEL");
    display.setCursor(0, 42);
    display.print("Err: ");
    display.print(latestSps30Error);
    display.display();
    return;
  }

  display.setCursor(0, 10);
  display.print("PM1.0 : ");
  drawValue(latestPm1p0, 1);
  display.print(" ug/m3");

  display.setCursor(0, 20);
  display.print("PM2.5 : ");
  drawValue(latestPm2p5, 1);
  display.print(" ug/m3");

  display.setCursor(0, 30);
  display.print("PM4.0 : ");
  drawValue(latestPm4p0, 1);
  display.print(" ug/m3");

  display.setCursor(0, 40);
  display.print("PM10  : ");
  drawValue(latestPm10p0, 1);
  display.print(" ug/m3");

  display.setCursor(0, 52);
  display.print("Size: ");
  drawValue(latestTypicalParticleSize, 2);
  display.print(" um");

  display.display();
}

void renderDisplay()
{
  if (!displayReady) return;

  if (popupUntilMs > millis()) {
    drawPopup();
    return;
  }

  switch (currentPage) {
    case 0:
      drawPageInfo();
      break;
    case 1:
      drawPageBMEFirst();
      break;
    case 2:
      drawPageBMESecond();
      break;
    case 3:
      drawPageBMEThird();
      break;
    case 4:
      drawPageSound();
      break;
    case 5:
      drawPageSPS30();
      break;
  }
}

void showErrorAndStop(const char *line1, int code)
{
  if (displayReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    drawCenteredText(line1, 14, 1);
    display.setCursor(18, 34);
    display.setTextSize(1);
    display.print("Code: ");
    display.print(code);
    display.display();
  }

  while (1) delay(1000);
}

void handleButton()
{
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != buttonLastReading) {
    lastButtonChangeMs = millis();
    buttonLastReading = reading;
  }

  if ((millis() - lastButtonChangeMs) < BUTTON_DEBOUNCE_MS) return;

  if (reading != buttonStableState) {
    buttonStableState = reading;

    if (buttonStableState == LOW) {
      currentPage = (currentPage + 1) % PAGE_COUNT;
      popupUntilMs = 0;
      renderDisplay();
    }
  }
}

void setup()
{
  Serial.begin(SERIAL_BAUD);

  Wire.begin();   // Nano ESP32: I2C mac dinh
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  initMax4466();

  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  showBootMessage("IEQ monitor", "Starting...");
  delay(900);
  connectWiFi();

  initBME688();
  initSPS30();

  showBootMessage("System ready", "Page 0: Info");
  delay(800);
  renderDisplay();
}

void loop()
{
  handleButton();

  updateBME688();
  updateMax4466();
  updateSPS30();

  if (millis() - lastDisplayMs >= DISPLAY_UPDATE_MS) {
    lastDisplayMs = millis();
    renderDisplay();
  }

  if (millis() - lastSerialMs >= SERIAL_SEND_MS) {
    lastSerialMs = millis();
    sendSerialSample();
  }

  if (millis() - lastHttpMs >= HTTP_SEND_MS) {
    lastHttpMs = millis();
    sendHttpSample();
  }
}
