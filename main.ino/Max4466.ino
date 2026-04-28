const int MAX4466_PIN = A0;
const float MAX4466_VREF = 3.3f;          // Nano ESP32 dung 3.3V
const int MAX4466_ADC_MAX = 4095;         // 12-bit
const int MAX4466_SAMPLE_COUNT = 300;     // Gon hon de khong block BSEC qua lau.
const uint32_t MAX4466_UPDATE_MS = 1000;

// Calibration MAX4466:
// dB_SPL = 20 * log10(V_rms / V_cal_rms) + dB_cal
// Do V_cal_rms khi dat microphone trong moi truong co dB_cal da biet,
// roi thay hai gia tri ben duoi de can chinh theo thiet bi that.
const float MAX4466_CAL_VRMS = 0.001f;
const float MAX4466_CAL_DB_SPL = 15.0f;
const float MAX4466_MIN_VRMS = 0.000001f;

float latestDb = NAN;
float latestVrms = NAN;
uint32_t lastMax4466Ms = 0;

void initMax4466()
{
  analogReadResolution(12);
}

void updateMax4466Reading()
{
  long sum = 0;

  for (int i = 0; i < MAX4466_SAMPLE_COUNT; i++) {
    sum += analogRead(MAX4466_PIN);
    delayMicroseconds(200);
  }

  float meanRaw = (float)sum / MAX4466_SAMPLE_COUNT;
  double sqSum = 0.0;

  for (int i = 0; i < MAX4466_SAMPLE_COUNT; i++) {
    float raw = analogRead(MAX4466_PIN);
    float centered = raw - meanRaw;
    sqSum += centered * centered;
    delayMicroseconds(200);
  }

  float rmsRaw = sqrt(sqSum / MAX4466_SAMPLE_COUNT);
  latestVrms = (rmsRaw / MAX4466_ADC_MAX) * MAX4466_VREF;

  float safeVrms = fmaxf(latestVrms, MAX4466_MIN_VRMS);
  latestDb = 20.0f * log10(safeVrms / MAX4466_CAL_VRMS) + MAX4466_CAL_DB_SPL;
}

void updateMax4466()
{
  if (millis() - lastMax4466Ms < MAX4466_UPDATE_MS) return;

  lastMax4466Ms = millis();
  updateMax4466Reading();
}
