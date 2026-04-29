const int MAX4466_PIN = A0;
const float MAX4466_VREF = 3.3f;          // Nano ESP32 dung 3.3V
const int MAX4466_ADC_MAX = 4095;         // 12-bit
const int MAX4466_SAMPLE_COUNT = 160;     // 20 ms window at roughly 8 kHz.
const uint32_t MAX4466_SAMPLE_DELAY_US = 125; // Nominal 8 kHz ADC sampling cadence.
const uint32_t MAX4466_UPDATE_MS = 100;

// Calibration MAX4466:
// dB_SPL = 20 * log10(V_rms / V_cal_rms) + dB_cal.
// Gia tri nay chi dung tuong doi cho den khi can bang bang sound meter that.
// Dat mic trong moi truong co dB_cal da biet, doc latestVrms, roi thay
// MAX4466_CAL_VRMS va MAX4466_CAL_DB_SPL ben duoi.
const float MAX4466_CAL_VRMS = 0.001f;
const float MAX4466_CAL_DB_SPL = 15.0f;
const float MAX4466_MIN_VRMS = 0.000001f;
const float MAX4466_RELEASE_ALPHA = 0.28f;

float latestDb = NAN;
float latestVrms = NAN;
uint32_t lastMax4466Ms = 0;

void initMax4466()
{
  analogReadResolution(12);
}

void updateMax4466Reading()
{
  double sum = 0.0;
  double squareSum = 0.0;
  int minRaw = MAX4466_ADC_MAX;
  int maxRaw = 0;

  for (int i = 0; i < MAX4466_SAMPLE_COUNT; i++) {
    int raw = analogRead(MAX4466_PIN);

    sum += raw;
    squareSum += (double)raw * raw;
    if (raw < minRaw) minRaw = raw;
    if (raw > maxRaw) maxRaw = raw;
    delayMicroseconds(MAX4466_SAMPLE_DELAY_US);
  }

  double meanRaw = sum / MAX4466_SAMPLE_COUNT;
  double varianceRaw = (squareSum / MAX4466_SAMPLE_COUNT) - (meanRaw * meanRaw);
  if (varianceRaw < 0.0) varianceRaw = 0.0;

  float rmsRaw = sqrt(varianceRaw);
  float rmsVrms = (rmsRaw / MAX4466_ADC_MAX) * MAX4466_VREF;
  float peakToPeakVrms = (((float)(maxRaw - minRaw) / MAX4466_ADC_MAX) * MAX4466_VREF) / (2.0f * sqrt(2.0f));

  latestVrms = fmaxf(rmsVrms, peakToPeakVrms);

  float safeVrms = fmaxf(latestVrms, MAX4466_MIN_VRMS);
  float instantDb = 20.0f * log10(safeVrms / MAX4466_CAL_VRMS) + MAX4466_CAL_DB_SPL;

  if (isnan(latestDb) || instantDb > latestDb) {
    latestDb = instantDb;
  } else {
    latestDb = (latestDb * (1.0f - MAX4466_RELEASE_ALPHA)) + (instantDb * MAX4466_RELEASE_ALPHA);
  }
}

void updateMax4466()
{
  if (millis() - lastMax4466Ms < MAX4466_UPDATE_MS) return;

  lastMax4466Ms = millis();
  updateMax4466Reading();
}
