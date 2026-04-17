#include <math.h>
#include <Wire.h>
#include <bsec2.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

// Adafruit BME688/BME680 mac dinh dung I2C 0x77.
// Tren breakout Adafruit: SCK -> SCL, SDI -> SDA, CS -> 3V3/bo ho, SDO -> bo ho.
// Neu noi SDO xuong GND thi doi thanh BME68X_I2C_ADDR_LOW.
#define BME688_I2C_ADDR      BME68X_I2C_ADDR_HIGH

// 6 gio, giong y tuong STATE_SAVE_PERIOD cua example Bosch
#define STATE_SAVE_PERIOD_MS (360UL * 60UL * 1000UL)

const int IEQ_SCORE = 100;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;
const int OLED_ADDRESS = 0x3C;

const int BUTTON_PIN = 6;         // Noi nut vao D6 va GND
const int PAGE_COUNT = 3;
const uint32_t BUTTON_DEBOUNCE_MS = 50;
const uint32_t DISPLAY_UPDATE_MS = 250;
const uint32_t POPUP_DURATION_MS = 1600;

const int MIC_PIN = A0;
const float VREF = 3.3f;          // Nano ESP32 dung 3.3V
const int ADC_MAX = 4095;         // 12-bit
const int SAMPLE_COUNT = 300;     // gon hon de khong block BSEC qua lau
const uint32_t SOUND_UPDATE_MS = 1000;

// Baseline microphone dang thap hon Apple Watch khoang 15 dB: 35 dB -> 50 dB.
// Neu can can chinh tiep, doi so nay.
const float SOUND_DB_OFFSET = 15.0f;
const float MIN_VRMS = 0.001f;

Bsec2 envSensor;
Preferences prefs;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE];

uint8_t latestIaqAccuracy = 0;
uint32_t lastStateSaveMs = 0;
bool firstStateSaved = false;

float latestIaq = NAN;
float latestStaticIaq = NAN;
float latestEco2 = NAN;
float latestBvoc = NAN;
float latestTemp = NAN;
float latestHum = NAN;
float latestPress = NAN;
float latestCompGas = NAN;

float latestDb = NAN;
float latestVrms = NAN;

bool displayReady = false;
uint8_t currentPage = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastSoundMs = 0;

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

void drawPageIEQ()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Page 1: IEQ");

  drawCenteredText("IEQ SCORE", 16, 1);

  display.setTextSize(3);
  display.setCursor(10, 34);
  display.print(IEQ_SCORE);
  display.setTextSize(2);
  display.print("/100");

  display.display();
}

void drawPageBME()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("Page 2: BME688");

  display.setCursor(0, 8);
  display.print("Temp: ");
  drawValue(latestTemp, 1);
  display.print(" C");

  display.setCursor(0, 16);
  display.print("Hum : ");
  drawValue(latestHum, 1);
  display.print(" %");

  display.setCursor(0, 24);
  display.print("Pres: ");
  drawValue(latestPress, 1);
  display.print(" hPa");

  display.setCursor(0, 32);
  display.print("IAQ : ");
  drawValue(latestIaq, 1);
  display.print(" Acc:");
  display.print(latestIaqAccuracy);

  display.setCursor(0, 40);
  display.print("eCO2: ");
  drawValue(latestEco2, 0);
  display.print(" ppm");

  display.setCursor(0, 48);
  display.print("bVOC: ");
  drawValue(latestBvoc, 3);
  display.print(" ppm");

  display.setCursor(0, 56);
  display.print("Gas : ");
  drawValue(latestCompGas, 0);
  display.print(" ohm");

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
  display.print("Page 3: Sound");

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

void renderDisplay()
{
  if (!displayReady) return;

  if (popupUntilMs > millis()) {
    drawPopup();
    return;
  }

  switch (currentPage) {
    case 0:
      drawPageIEQ();
      break;
    case 1:
      drawPageBME();
      break;
    case 2:
      drawPageSound();
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

void checkBsecStatus()
{
  if (envSensor.status < BSEC_OK) {
    showErrorAndStop("BSEC error", envSensor.status);
  } else if (envSensor.status > BSEC_OK) {
    showPopup("BSEC warning", "Check sensor");
  }

  if (envSensor.sensor.status < BME68X_OK) {
    showErrorAndStop("BME688 error", envSensor.sensor.status);
  } else if (envSensor.sensor.status > BME68X_OK) {
    showPopup("BME688 warning", "Check sensor");
  }
}

bool loadStateFromNVS()
{
  showBootMessage("BSEC state", "Loading...");
  delay(700);

  size_t len = prefs.getBytesLength("state");

  if (len != BSEC_MAX_STATE_BLOB_SIZE) {
    showBootMessage("BSEC state", "No saved state");
    delay(1100);
    return true;
  }

  size_t readLen = prefs.getBytes("state", bsecState, BSEC_MAX_STATE_BLOB_SIZE);
  if (readLen != BSEC_MAX_STATE_BLOB_SIZE) {
    showBootMessage("BSEC state", "Read failed");
    delay(1100);
    return false;
  }

  if (!envSensor.setState(bsecState)) {
    showBootMessage("BSEC state", "Load failed");
    delay(1100);
    return false;
  }

  firstStateSaved = true;
  lastStateSaveMs = millis();

  showBootMessage("BSEC state", "Loaded");
  delay(1100);
  return true;
}

bool saveStateToNVS()
{
  if (!envSensor.getState(bsecState)) {
    showPopup("BSEC state", "Get failed");
    return false;
  }

  size_t written = prefs.putBytes("state", bsecState, BSEC_MAX_STATE_BLOB_SIZE);
  if (written != BSEC_MAX_STATE_BLOB_SIZE) {
    showPopup("BSEC state", "Save failed");
    return false;
  }

  lastStateSaveMs = millis();
  firstStateSaved = true;
  showPopup("BSEC state", "Saved to NVS");
  return true;
}

void maybeSaveState()
{
  bool shouldSave = false;

  // Luu lan dau khi BSEC da calibrate tot nhat
  if (!firstStateSaved && latestIaqAccuracy == 3) {
    shouldSave = true;
  }

  // Sau do luu dinh ky
  if (firstStateSaved && (millis() - lastStateSaveMs >= STATE_SAVE_PERIOD_MS)) {
    shouldSave = true;
  }

  if (shouldSave) {
    if (!saveStateToNVS()) {
      checkBsecStatus();
    }
  }
}

void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec)
{
  (void)data;
  (void)bsec;

  if (!outputs.nOutputs) return;

  for (uint8_t i = 0; i < outputs.nOutputs; i++) {
    const bsecData output = outputs.output[i];

    switch (output.sensor_id) {
      case BSEC_OUTPUT_IAQ:
        latestIaq = output.signal;
        latestIaqAccuracy = output.accuracy;
        break;

      case BSEC_OUTPUT_STATIC_IAQ:
        latestStaticIaq = output.signal;
        break;

      case BSEC_OUTPUT_CO2_EQUIVALENT:
        latestEco2 = output.signal;
        break;

      case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
        latestBvoc = output.signal;
        break;

      case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
        latestTemp = output.signal;
        break;

      case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
        latestHum = output.signal;
        break;

      case BSEC_OUTPUT_RAW_PRESSURE:
        latestPress = output.signal / 100.0f; // Pa -> hPa
        break;

      case BSEC_OUTPUT_COMPENSATED_GAS:
        latestCompGas = output.signal;
        break;

      default:
        break;
    }
  }
}

void updateSoundReading()
{
  long sum = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++) {
    sum += analogRead(MIC_PIN);
    delayMicroseconds(200);
  }

  float meanRaw = (float)sum / SAMPLE_COUNT;
  double sqSum = 0.0;

  for (int i = 0; i < SAMPLE_COUNT; i++) {
    float raw = analogRead(MIC_PIN);
    float centered = raw - meanRaw;
    sqSum += centered * centered;
    delayMicroseconds(200);
  }

  float rmsRaw = sqrt(sqSum / SAMPLE_COUNT);
  latestVrms = (rmsRaw / ADC_MAX) * VREF;

  float dbRelative = 20.0f * log10(fmaxf(latestVrms, MIN_VRMS) / MIN_VRMS);
  latestDb = dbRelative + SOUND_DB_OFFSET;
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
  Wire.begin();   // Nano ESP32: I2C mac dinh
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  analogReadResolution(12);

  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  showBootMessage("IEQ monitor", "Starting...");
  delay(900);

  if (!prefs.begin("bsec2", false)) {
    showErrorAndStop("NVS error", 0);
  }

  if (!envSensor.begin(BME688_I2C_ADDR, Wire)) {
    checkBsecStatus();
  }

  // Load state cu truoc khi subscribe outputs
  if (!loadStateFromNVS()) {
    checkBsecStatus();
  }

  bsecSensor sensorList[] = {
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_COMPENSATED_GAS
  };

  if (!envSensor.updateSubscription(sensorList, ARRAY_LEN(sensorList), BSEC_SAMPLE_RATE_LP)) {
    checkBsecStatus();
  }

  envSensor.attachCallback(newDataCallback);
  checkBsecStatus();

  showBootMessage("System ready", "Page 1: IEQ");
  delay(800);
  renderDisplay();
}

void loop()
{
  handleButton();

  if (!envSensor.run()) {
    checkBsecStatus();
  }

  maybeSaveState();

  if (millis() - lastSoundMs >= SOUND_UPDATE_MS) {
    lastSoundMs = millis();
    updateSoundReading();
  }

  if (millis() - lastDisplayMs >= DISPLAY_UPDATE_MS) {
    lastDisplayMs = millis();
    renderDisplay();
  }
}
