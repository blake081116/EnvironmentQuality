#define BME688_ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

// Adafruit BME688/BME680 mac dinh dung I2C 0x77.
// Tren breakout Adafruit: SCK -> SCL, SDI -> SDA, CS -> 3V3/bo ho, SDO -> bo ho.
// Neu noi SDO xuong GND thi doi thanh BME68X_I2C_ADDR_LOW.
#define BME688_I2C_ADDR      BME68X_I2C_ADDR_HIGH

// 6 gio, giong y tuong STATE_SAVE_PERIOD cua example Bosch.
#define STATE_SAVE_PERIOD_MS (360UL * 60UL * 1000UL)

Bsec2 bme688Sensor;
Preferences bme688Prefs;

uint8_t bme688State[BSEC_MAX_STATE_BLOB_SIZE];

uint8_t latestIaqAccuracy = 0;
uint32_t bme688LastStateSaveMs = 0;
bool bme688FirstStateSaved = false;

float latestIaq = NAN;
float latestStaticIaq = NAN;
float latestEco2 = NAN;
float latestBvoc = NAN;
float latestRawTemp = NAN;
float latestTemp = NAN;
float latestRawHum = NAN;
float latestHum = NAN;
float latestPress = NAN;
float latestRawGas = NAN;
float latestCompGas = NAN;
float latestGasPercentage = NAN;
float latestStabilizationStatus = NAN;

void checkBME688Status()
{
  if (bme688Sensor.status < BSEC_OK) {
    showErrorAndStop("BSEC error", bme688Sensor.status);
  } else if (bme688Sensor.status > BSEC_OK) {
    showPopup("BSEC warning", "Check sensor");
  }

  if (bme688Sensor.sensor.status < BME68X_OK) {
    showErrorAndStop("BME688 error", bme688Sensor.sensor.status);
  } else if (bme688Sensor.sensor.status > BME68X_OK) {
    showPopup("BME688 warning", "Check sensor");
  }
}

bool loadBME688StateFromNVS()
{
  showBootMessage("BSEC state", "Loading...");
  delay(700);

  size_t len = bme688Prefs.getBytesLength("state");

  if (len != BSEC_MAX_STATE_BLOB_SIZE) {
    showBootMessage("BSEC state", "No saved state");
    Serial.println("BSEC state: no saved state in NVS.");
    delay(1100);
    return true;
  }

  size_t readLen = bme688Prefs.getBytes("state", bme688State, BSEC_MAX_STATE_BLOB_SIZE);
  if (readLen != BSEC_MAX_STATE_BLOB_SIZE) {
    showBootMessage("BSEC state", "Read failed");
    Serial.println("BSEC state: read failed.");
    delay(1100);
    return false;
  }

  if (!bme688Sensor.setState(bme688State)) {
    showBootMessage("BSEC state", "Load failed");
    Serial.println("BSEC state: load into BSEC failed.");
    delay(1100);
    return false;
  }

  bme688FirstStateSaved = true;
  bme688LastStateSaveMs = millis();

  showBootMessage("BSEC state", "Loaded");
  Serial.println("BSEC state: loaded from NVS.");
  delay(1100);
  return true;
}

bool saveBME688StateToNVS()
{
  if (!bme688Sensor.getState(bme688State)) {
    showPopup("BSEC state", "Get failed");
    Serial.println("BSEC state: getState failed.");
    return false;
  }

  size_t written = bme688Prefs.putBytes("state", bme688State, BSEC_MAX_STATE_BLOB_SIZE);
  if (written != BSEC_MAX_STATE_BLOB_SIZE) {
    showPopup("BSEC state", "Save failed");
    Serial.println("BSEC state: save to NVS failed.");
    return false;
  }

  bme688LastStateSaveMs = millis();
  bme688FirstStateSaved = true;
  showPopup("BSEC state", "Saved to NVS");
  Serial.println("BSEC state: saved to NVS.");
  return true;
}

void maybeSaveBME688State()
{
  bool shouldSave = false;

  // Luu lan dau khi BSEC da calibrate tot nhat.
  if (!bme688FirstStateSaved && latestIaqAccuracy == 3) {
    shouldSave = true;
  }

  // Sau do luu dinh ky.
  if (bme688FirstStateSaved && (millis() - bme688LastStateSaveMs >= STATE_SAVE_PERIOD_MS)) {
    shouldSave = true;
  }

  if (shouldSave) {
    if (!saveBME688StateToNVS()) {
      checkBME688Status();
    }
  }
}

void newBME688DataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec)
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

      case BSEC_OUTPUT_RAW_TEMPERATURE:
        latestRawTemp = output.signal;
        break;

      case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
        latestTemp = output.signal;
        break;

      case BSEC_OUTPUT_RAW_HUMIDITY:
        latestRawHum = output.signal;
        break;

      case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
        latestHum = output.signal;
        break;

      case BSEC_OUTPUT_RAW_PRESSURE:
        latestPress = output.signal / 100.0f; // Pa -> hPa
        break;

      case BSEC_OUTPUT_RAW_GAS:
        latestRawGas = output.signal;
        break;

      case BSEC_OUTPUT_STABILIZATION_STATUS:
        latestStabilizationStatus = output.signal;
        break;

      case BSEC_OUTPUT_COMPENSATED_GAS:
        latestCompGas = output.signal;
        break;

      case BSEC_OUTPUT_GAS_PERCENTAGE:
        latestGasPercentage = output.signal;
        break;

      default:
        break;
    }
  }
}

void initBME688()
{
  if (!bme688Prefs.begin("bsec2", false)) {
    showErrorAndStop("NVS error", 0);
  }

  if (!bme688Sensor.begin(BME688_I2C_ADDR, Wire)) {
    checkBME688Status();
  }

  // Load state cu truoc khi subscribe outputs.
  if (!loadBME688StateFromNVS()) {
    checkBME688Status();
  }

  bsecSensor sensorList[] = {
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_STABILIZATION_STATUS,
    BSEC_OUTPUT_COMPENSATED_GAS,
    BSEC_OUTPUT_GAS_PERCENTAGE
  };

  if (!bme688Sensor.updateSubscription(sensorList, BME688_ARRAY_LEN(sensorList), BSEC_SAMPLE_RATE_LP)) {
    checkBME688Status();
  }

  bme688Sensor.attachCallback(newBME688DataCallback);
  checkBME688Status();
}

void updateBME688()
{
  if (!bme688Sensor.run()) {
    checkBME688Status();
  }

  maybeSaveBME688State();
}
