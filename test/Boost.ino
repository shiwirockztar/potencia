#include <Arduino.h>

// Pines
const int PIN_PWM_GENERADOR = 26;
const int PIN_PWM_CONTROL = 25;
const int PIN_ADC_CORRIENTE = 34;
const int PIN_SALIDA_FILTRADA = 33;
const int PIN_IREF_DIGITAL = 27;

// PWM y tiempos
const uint32_t PWM_FREQ = 20000;
const uint8_t PWM_RESOLUTION = 10;
const uint32_t PWM_MAX = (1U << PWM_RESOLUTION) - 1U;
const uint32_t CONTROL_PERIOD_US = 100;
const uint32_t IREF_PERIOD_MS = 20;
const uint32_t PRINT_PERIOD_US = 50000;

// Referencia de corriente
const float IREF_LOW = 1.0f;
const float IREF_HIGH = 2.0f;
const float IREF_PWM_MAX = 3.0f;

// ACS712
const float DIVISOR_RATIO = 20.0f / 30.0f;
const float ACS712_SENS = 0.1891f;
const int ADC_SAMPLES = 8;
const int OFFSET_SAMPLES = 128;
const float FILTER_ALPHA = 0.05f;
const float CURRENT_OUTPUT_MAX = 3.0f;
float acs712OffsetV = 2.3853f;

// Control PI y limitador
const float KP = 0.4912f;
const float KI = 750.2f;
const float TS = CONTROL_PERIOD_US * 1e-6f;
const float CURRENT_MAX = 3.0f;
const float CURRENT_LIMIT_GAIN = 4.0f;

float iref = IREF_LOW;
float currentFiltered = 0.0f;
float integrator = 0.0f;
float previousError = 0.0f;
float duty = 0.0f;
bool highReference = false;
uint32_t lastControlUs = 0;
uint32_t lastIrefMs = 0;
uint32_t lastPrintUs = 0;

void updateReference(uint32_t nowMs) {
  if (nowMs - lastIrefMs < IREF_PERIOD_MS)
    return;

  lastIrefMs = nowMs;
  highReference = !highReference;
  iref = highReference ? IREF_HIGH : IREF_LOW;

  digitalWrite(PIN_IREF_DIGITAL, highReference);
  ledcWrite(PIN_PWM_GENERADOR, (uint32_t)(iref / IREF_PWM_MAX * PWM_MAX));
}

float readCurrent() {
  uint32_t sum = 0;

  for (int i = 0; i < ADC_SAMPLES; i++)
    sum += analogReadMilliVolts(PIN_ADC_CORRIENTE);

  const float adcV = sum / (float)ADC_SAMPLES / 1000.0f;
  return (adcV / DIVISOR_RATIO - acs712OffsetV) / ACS712_SENS;
}

float piControl(float error) {
  const float integralStep = KI * TS * 0.5f * (error + previousError);
  const float unsaturated = KP * error + integrator + integralStep;
  const bool saturatedHigh = unsaturated > 1.0f;
  const bool saturatedLow = unsaturated < 0.0f;

  if (!((saturatedHigh && error > 0.0f) ||
        (saturatedLow && error < 0.0f)))
    integrator += integralStep;

  previousError = error;
  return constrain(KP * error + integrator, 0.0f, 1.0f);
}

void calibrateCurrentSensor() {
  uint32_t sum = 0;

  for (int i = 0; i < OFFSET_SAMPLES; i++)
    sum += analogReadMilliVolts(PIN_ADC_CORRIENTE);

  acs712OffsetV =
      (sum / (float)OFFSET_SAMPLES / 1000.0f) / DIVISOR_RATIO;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_IREF_DIGITAL, OUTPUT);
  digitalWrite(PIN_IREF_DIGITAL, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_ADC_CORRIENTE, ADC_11db);
  calibrateCurrentSensor();

  ledcAttach(PIN_PWM_GENERADOR, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(PIN_PWM_GENERADOR, (uint32_t)(IREF_LOW / IREF_PWM_MAX * PWM_MAX));

  ledcAttach(PIN_PWM_CONTROL, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(PIN_PWM_CONTROL, 0);

  ledcAttach(PIN_SALIDA_FILTRADA, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(PIN_SALIDA_FILTRADA, 0);

  lastControlUs = micros();
  lastIrefMs = millis();
  lastPrintUs = micros();

  Serial.println("=== Control PI Boost ===");
  Serial.printf("Iref=%.1f..%.1f A | Kp=%.4f | Ki=%.2f\n",
                IREF_LOW, IREF_HIGH, KP, KI);
}

void loop() {
  const uint32_t nowUs = micros();
  updateReference(millis());

  if (nowUs - lastControlUs < CONTROL_PERIOD_US)
    return;

  lastControlUs = nowUs;

  const float measuredCurrent = readCurrent();
  currentFiltered += FILTER_ALPHA * (measuredCurrent - currentFiltered);

  const float filteredDuty =
      constrain(currentFiltered / CURRENT_OUTPUT_MAX, 0.0f, 1.0f);
  ledcWrite(PIN_SALIDA_FILTRADA, (uint32_t)(filteredDuty * PWM_MAX));

  // Se conserva el signo usado por la sintonizacion original del Boost.
  const float error = iref + measuredCurrent;
  duty = piControl(error);

  if (measuredCurrent > CURRENT_MAX) {
    duty -= CURRENT_LIMIT_GAIN * (measuredCurrent - CURRENT_MAX);
    integrator = 0.0f;
  }

  duty = constrain(duty, 0.0f, 1.0f);
  ledcWrite(PIN_PWM_CONTROL, (uint32_t)(duty * PWM_MAX));

  if (nowUs - lastPrintUs >= PRINT_PERIOD_US) {
    lastPrintUs = nowUs;
    Serial.printf("Iref=%.2f A | I=%.3f A | Error=%.3f | Duty=%.2f%%\n",
                  iref, measuredCurrent, error, duty * 100.0f);
  }
}
