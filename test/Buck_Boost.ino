#include <Arduino.h>

// Pines
const int PIN_PWM = 26;
const int PIN_ADC = 27;
const int PIN_SYNC = 25;

// PWM y tiempos
const uint32_t PWM_FREQ = 20000;
const uint8_t PWM_RESOLUTION = 10;
const uint32_t PWM_MAX = (1U << PWM_RESOLUTION) - 1U;
const uint32_t PRINT_PERIOD_US = 50000;

// Control PI
float Kp = 0.01f;
float Ki = 35.0f;
float setpoint = 5.0f;

// Estado del controlador
float voltage = 0.0f;
float integral = 0.0f;
float duty = 0.0f;
uint32_t lastControlUs = 0;
uint32_t lastPrintUs = 0;
String command = "";

void updateControl(uint32_t now);
void handleSerial();
void printValues(uint32_t now);
void boostSetup();
void boostUpdate(uint32_t nowUs, uint32_t nowMs);

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // Sync pin
  pinMode(PIN_SYNC, OUTPUT);
  digitalWrite(PIN_SYNC, LOW);

  // ADC
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_ADC, ADC_11db);

  // PWM
  ledcAttach(PIN_PWM, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(PIN_PWM, 0);

  Serial.println("=== Control PI Buck Converter ===");
  Serial.printf("  Setpoint=%.2fV | Kp=%.4f | Ki=%.3f\n", setpoint, Kp, Ki);
  Serial.println("Setpoint,Voltage,Duty_x10");

  boostSetup();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  const uint32_t now = micros();

  updateControl(now);
  handleSerial();
  printValues(now);
  boostUpdate(now, millis());
}

// ============================================================
//  FUNCIONES
// ============================================================

void updateControl(uint32_t now) {
  //voltage = analogReadMilliVolts(PIN_ADC) * 0.01028f; //calibracion lineal
  //voltage = analogReadMilliVolts(PIN_ADC) * 0.010645260f - 0.152471264 calibracion 
  const float measuredVoltage = analogReadMilliVolts(PIN_ADC) * 0.010645260f - 0.152471264f; // Calibración polinomial
  voltage = -0.236816150f + measuredVoltage * (1.098464112f + measuredVoltage * (-0.010294477f + 0.000325122f * measuredVoltage));
  const float error = setpoint - voltage;
  const float dt = (now - lastControlUs) * 1e-6f;

  integral += Ki * error * dt;
  integral = constrain(integral, 0.05f, 0.95f);
  duty = Kp * error + integral;
  duty = constrain(duty, 0.05f, 0.95f);
  ledcWrite(PIN_PWM, (uint32_t)(duty * PWM_MAX));
  lastControlUs = now;
}

void printValues(uint32_t now) {
  if (now - lastPrintUs >= PRINT_PERIOD_US) {
    lastPrintUs = now;
    Serial.printf("%.3f,%.3f,%.3f\n", setpoint, voltage, duty * 10.0f);
  }
}

void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      command.trim();
      if      (command.startsWith("kp="))  Kp = command.substring(3).toFloat();
      else if (command.startsWith("ki="))  Ki = command.substring(3).toFloat();
      else if (command.startsWith("sp="))  setpoint = command.substring(3).toFloat();
      else if (command.equalsIgnoreCase("info")) {
        Serial.println("--- INFO ---");
        Serial.printf("  Setpoint = %.2f V\n",  setpoint);
        Serial.printf("  Voltage = %.3f V\n",  voltage);
        Serial.printf("  Duty     = %.3f\n",    duty);
        Serial.printf("  Kp       = %.4f\n",    Kp);
        Serial.printf("  Ki       = %.3f\n",    Ki);
        Serial.println("------------");
      }
      command = "";
    } else {
      command += c;
    }
  }
}

  // ============================================================
  //  BOOST
  // ============================================================

  // GPIO 33 y 32 son las dos salidas PWM del Boost.
  // GPIO 35 es entrada ADC para el ACS712.
  const int BOOST_PWM_GENERATOR = 33;
  const int BOOST_PWM_CONTROL = 32;
  const int BOOST_ADC_CURRENT = 35;

  const uint32_t BOOST_CONTROL_PERIOD_US = 100;
  const uint32_t BOOST_IREF_PERIOD_MS = 20;
  const float BOOST_IREF_LOW = 1.0f;
  const float BOOST_IREF_HIGH = 2.0f;
  const float BOOST_IREF_PWM_MAX = 3.0f;
  const float BOOST_DIVISOR_RATIO = 20.0f / 30.0f;
  const float BOOST_ACS712_SENS = 0.1891f;
  const int BOOST_ADC_SAMPLES = 8;
  const int BOOST_OFFSET_SAMPLES = 128;
  const float BOOST_FILTER_ALPHA = 0.05f;
  const float BOOST_CURRENT_MAX = 3.0f;
  const float BOOST_CURRENT_LIMIT_GAIN = 4.0f;
  const float BOOST_KP = 0.4912f;
  const float BOOST_KI = 750.2f;
  const float BOOST_TS = BOOST_CONTROL_PERIOD_US * 1e-6f;

  float boostIref = BOOST_IREF_LOW;
  float boostCurrentFiltered = 0.0f;
  float boostIntegrator = 0.0f;
  float boostPreviousError = 0.0f;
  float boostDuty = 0.0f;
  float boostOffsetV = 2.3853f;
  bool boostHighReference = false;
  uint32_t boostLastControlUs = 0;
  uint32_t boostLastIrefMs = 0;

  float boostReadCurrent() {
    uint32_t sum = 0;

    for (int i = 0; i < BOOST_ADC_SAMPLES; i++)
      sum += analogReadMilliVolts(BOOST_ADC_CURRENT);

    const float adcV = sum / (float)BOOST_ADC_SAMPLES / 1000.0f;
    return (adcV / BOOST_DIVISOR_RATIO - boostOffsetV) / BOOST_ACS712_SENS;
  }

  float boostPiControl(float error) {
    const float integralStep =
        BOOST_KI * BOOST_TS * 0.5f * (error + boostPreviousError);
    const float unsaturated =
        BOOST_KP * error + boostIntegrator + integralStep;
    const bool saturatedHigh = unsaturated > 1.0f;
    const bool saturatedLow = unsaturated < 0.0f;

    if (!((saturatedHigh && error > 0.0f) ||
          (saturatedLow && error < 0.0f)))
      boostIntegrator += integralStep;

    boostPreviousError = error;
    return constrain(BOOST_KP * error + boostIntegrator, 0.0f, 1.0f);
  }

  void boostCalibrateCurrent() {
    uint32_t sum = 0;

    for (int i = 0; i < BOOST_OFFSET_SAMPLES; i++)
      sum += analogReadMilliVolts(BOOST_ADC_CURRENT);

    boostOffsetV =
        (sum / (float)BOOST_OFFSET_SAMPLES / 1000.0f) /
        BOOST_DIVISOR_RATIO;
  }

  void boostSetup() {
    analogSetPinAttenuation(BOOST_ADC_CURRENT, ADC_11db);
    boostCalibrateCurrent();

    ledcAttach(BOOST_PWM_GENERATOR, PWM_FREQ, PWM_RESOLUTION);
    ledcWrite(
        BOOST_PWM_GENERATOR,
        (uint32_t)(BOOST_IREF_LOW / BOOST_IREF_PWM_MAX * PWM_MAX)
    );

    ledcAttach(BOOST_PWM_CONTROL, PWM_FREQ, PWM_RESOLUTION);
    ledcWrite(BOOST_PWM_CONTROL, 0);

    boostLastControlUs = micros();
    boostLastIrefMs = millis();

    Serial.println("=== Control PI Boost ===");
    Serial.printf("PWM generador=%d | PWM control=%d | ACS712=%d\n",
                  BOOST_PWM_GENERATOR, BOOST_PWM_CONTROL, BOOST_ADC_CURRENT);
  }

  void boostUpdate(uint32_t nowUs, uint32_t nowMs) {
    if (nowMs - boostLastIrefMs >= BOOST_IREF_PERIOD_MS) {
      boostLastIrefMs = nowMs;
      boostHighReference = !boostHighReference;
      boostIref = boostHighReference ? BOOST_IREF_HIGH : BOOST_IREF_LOW;
      ledcWrite(
          BOOST_PWM_GENERATOR,
          (uint32_t)(boostIref / BOOST_IREF_PWM_MAX * PWM_MAX)
      );
    }

    if (nowUs - boostLastControlUs < BOOST_CONTROL_PERIOD_US)
      return;

    boostLastControlUs = nowUs;
    const float measuredCurrent = boostReadCurrent();
    const float error = boostIref + measuredCurrent;

    boostCurrentFiltered +=
        BOOST_FILTER_ALPHA * (measuredCurrent - boostCurrentFiltered);
    boostDuty = boostPiControl(error);

    if (measuredCurrent > BOOST_CURRENT_MAX) {
      boostDuty -= BOOST_CURRENT_LIMIT_GAIN *
                   (measuredCurrent - BOOST_CURRENT_MAX);
      boostIntegrator = 0.0f;
    }

    boostDuty = constrain(boostDuty, 0.0f, 1.0f);
    ledcWrite(BOOST_PWM_CONTROL, (uint32_t)(boostDuty * PWM_MAX));
}