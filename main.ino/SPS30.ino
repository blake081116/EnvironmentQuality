const uint8_t SPS30_I2C_ADDRESS = 0x69;
const uint32_t SPS30_UPDATE_MS = 1000;
const uint32_t SPS30_RETRY_MS = 5000;
const int16_t SPS30_OK = 0;

SensirionI2cSps30 sps30Sensor;

float latestPm1p0 = NAN;
float latestPm2p5 = NAN;
float latestPm4p0 = NAN;
float latestPm10p0 = NAN;
float latestNc0p5 = NAN;
float latestNc1p0 = NAN;
float latestNc2p5 = NAN;
float latestNc4p0 = NAN;
float latestNc10p0 = NAN;
float latestTypicalParticleSize = NAN;
bool latestSps30Ready = false;
int16_t latestSps30Error = SPS30_OK;

uint32_t lastSps30Ms = 0;
uint32_t lastSps30RetryMs = 0;

bool sps30CommandOk(int16_t error)
{
  latestSps30Error = error;
  latestSps30Ready = (error == SPS30_OK);
  return latestSps30Ready;
}

void initSPS30Internal(bool showStatus)
{
  sps30Sensor.begin(Wire, SPS30_I2C_ADDRESS);

  sps30Sensor.wakeUpSequence();
  sps30Sensor.stopMeasurement();
  delay(100);

  if (!sps30CommandOk(sps30Sensor.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT))) {
    if (showStatus) {
      showBootMessage("SPS30", "Start failed");
      delay(900);
    }
    return;
  }

  if (showStatus) {
    showBootMessage("SPS30", "Ready");
    delay(500);
  }
}

void initSPS30()
{
  initSPS30Internal(true);
}

void updateSPS30Reading()
{
  uint16_t dataReadyFlag = 0;
  int16_t error = sps30Sensor.readDataReadyFlag(dataReadyFlag);

  if (!sps30CommandOk(error) || dataReadyFlag == 0) return;

  float pm1p0;
  float pm2p5;
  float pm4p0;
  float pm10p0;
  float nc0p5;
  float nc1p0;
  float nc2p5;
  float nc4p0;
  float nc10p0;
  float typicalParticleSize;

  error = sps30Sensor.readMeasurementValuesFloat(
    pm1p0,
    pm2p5,
    pm4p0,
    pm10p0,
    nc0p5,
    nc1p0,
    nc2p5,
    nc4p0,
    nc10p0,
    typicalParticleSize
  );

  if (!sps30CommandOk(error)) return;

  latestPm1p0 = pm1p0;
  latestPm2p5 = pm2p5;
  latestPm4p0 = pm4p0;
  latestPm10p0 = pm10p0;
  latestNc0p5 = nc0p5;
  latestNc1p0 = nc1p0;
  latestNc2p5 = nc2p5;
  latestNc4p0 = nc4p0;
  latestNc10p0 = nc10p0;
  latestTypicalParticleSize = typicalParticleSize;
}

void updateSPS30()
{
  if (!latestSps30Ready) {
    if (millis() - lastSps30RetryMs < SPS30_RETRY_MS) return;

    lastSps30RetryMs = millis();
    initSPS30Internal(false);
    return;
  }

  if (millis() - lastSps30Ms < SPS30_UPDATE_MS) return;

  lastSps30Ms = millis();
  updateSPS30Reading();
}
