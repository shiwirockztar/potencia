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
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  const uint32_t now = micros();

  updateControl(now);
  handleSerial();
  printValues(now);
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