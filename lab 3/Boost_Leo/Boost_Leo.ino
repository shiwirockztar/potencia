#include <Arduino.h>

// =============================================================
// CONFIGURACIÓN GENERAL
// =============================================================

struct SensorConfig {
  int adcPin;
  float adcVref;
  float adcMaxCount;
  float adcOffsetV;
  float dividerRatio;
  float sensitivityVPerA;
  float zeroCurrentVoltage;
};

struct PwmConfig {
  int pwmPin;
  int channel;
  int freqHz;
  int resolutionBits;
  float dutyNominal;
  float dutyMin;
  float dutyMax;
};

constexpr float D_NOMINAL = 0.4117f;

struct LedPins {
  int ok;
  int fault;
};

struct ControlConfig {
  float Ts;
  float setpointA;
  float kp;
  float ki;
  float kd;
  float integralLimit;
};

struct ControlState {
  float integral;
  float prevError;
};

// Ajustar estos valores según el sensor real y la electrónica conectada.
// Mismo pinout que boost_control_esp32.ino
SensorConfig sensorCfg = {
  34,            // ADC pin (GPIO34) = ACS712 -> ADC1_CH6
  3.3f,          // Referencia del ADC del ESP32
  4095.0f,       // Counts máximos en 12 bits
  0.0f,          // Offset de calibración en V (ajustar con medición real)
  15.0f / (10.0f + 15.0f),  // Divisor: R1=10k, R2=15k -> 0.6
  0.185f,        // Sensibilidad del ACS712-05B en V/A
  5.0f / 2.0f    // Tensión a corriente cero para ACS alimentado a 5 V: 2.5 V
};

PwmConfig pwmCfg = {
  25,            // PWM pin (GPIO25) = UCC21520 INA
  0,             // Canal LEDC
  20000,         // Frecuencia de PWM
  10,            // Resolución del PWM
  D_NOMINAL,     // Duty nominal recomendado
  0.0f,          // Límite inferior
  0.55f          // Límite superior
};

LedPins ledPins = {
  2,             // LED verde OK
  4              // LED rojo falla
};

ControlConfig ctrlCfg = {
  0.0001f,       // Ts = 100 us (10 kHz)
  1.5f,          // Setpoint en A (configurable)
  0.4912f,       // Kp del modelo C(s) = Kp + Ki/s
  750.2f,        // Ki [1/s] del modelo C(s) = Kp + Ki/s
  0.0f,          // No se usa termino derivativo en el modelo PI
  0.0f           // Limite opcional de integral. 0 = sin limite adicional
};

ControlState ctrlState = {0.0f, 0.0f};

volatile bool controlTick = false;
float currentA = 0.0f;
float errorA = 0.0f;
float dutyCycle = 0.0f;
uint32_t lastSampleUs = 0;

// =============================================================
// FUNCIONES AUXILIARES
// =============================================================

void printConfig(void) {
  Serial.println("===========================================");
  Serial.println("Boost_Leo.ino - Controlador Boost ESP32");
  Serial.println("===========================================");
  Serial.printf("ADC pin       : %d\n", sensorCfg.adcPin);
  Serial.printf("ADC Vref      : %.3f V\n", sensorCfg.adcVref);
  Serial.printf("ADC max count : %.0f\n", sensorCfg.adcMaxCount);
  Serial.printf("ACS sens      : %.4f V/A\n", sensorCfg.sensitivityVPerA);
  Serial.printf("ACS offset    : %.4f V\n", sensorCfg.adcOffsetV);
  Serial.printf("Divider ratio : %.4f\n", sensorCfg.dividerRatio);
  Serial.printf("PWM pin       : %d\n", pwmCfg.pwmPin);
  Serial.printf("LED OK/Fault  : %d / %d\n", ledPins.ok, ledPins.fault);
  Serial.printf("PWM freq      : %d Hz\n", pwmCfg.freqHz);
  Serial.printf("PWM bits      : %d\n", pwmCfg.resolutionBits);
  Serial.printf("D_nominal     : %.4f\n", pwmCfg.dutyNominal);
  Serial.printf("D_min / D_max : %.4f / %.4f\n", pwmCfg.dutyMin, pwmCfg.dutyMax);
  Serial.printf("Ts           : %.6f s\n", ctrlCfg.Ts);
  Serial.printf("Setpoint      : %.3f A\n", ctrlCfg.setpointA);
  Serial.println("===========================================\n");
}

void applyDuty(float duty) {
  duty = constrain(duty, pwmCfg.dutyMin, pwmCfg.dutyMax);
  dutyCycle = duty;

  uint32_t maxCounts = (1u << pwmCfg.resolutionBits) - 1u;
  uint32_t dutyCounts = (uint32_t)(duty * (float)maxCounts);
  ledcWrite(pwmCfg.channel, dutyCounts);
}

float readCurrentA(void) {
  int raw = analogRead(sensorCfg.adcPin);

  // ADC -> tensión en el pin del ESP32
  float vAdc = ((float)raw / sensorCfg.adcMaxCount) * sensorCfg.adcVref + sensorCfg.adcOffsetV;

  // Si hay divisor, quitarlo para recuperar la tensión del sensor.
  float vSensor = vAdc / sensorCfg.dividerRatio;

  // ACS: V = Vzero + sensitivity * I
  float currentA = (vSensor - sensorCfg.zeroCurrentVoltage) / sensorCfg.sensitivityVPerA;
  return currentA;
}

void setupPWM(void) {
  ledcSetup(pwmCfg.channel, pwmCfg.freqHz, pwmCfg.resolutionBits);
  ledcAttachPin(pwmCfg.pwmPin, pwmCfg.channel);
  ledcWrite(pwmCfg.channel, 0);
}

// =============================================================
// CONTROLADOR PI DISCRETO
// =============================================================
// C(s) = Kp + Ki/s
// Integrador discretizado con Tustin:
// I[n] = I[n-1] + Ki*Ts/2*(e[n] + e[n-1])
// La salida es una correccion de duty alrededor de D_NOMINAL.
// =============================================================
float computeControlAction(float error) {
  const float deltaIntegral = ctrlCfg.ki * ctrlCfg.Ts * 0.5f *
                              (error + ctrlState.prevError);
  const float actionUnsaturated = ctrlCfg.kp * error +
                                  ctrlState.integral + deltaIntegral;
  const float dutyUnsaturated = pwmCfg.dutyNominal + actionUnsaturated;
  const bool saturatedHigh = dutyUnsaturated > pwmCfg.dutyMax;
  const bool saturatedLow = dutyUnsaturated < pwmCfg.dutyMin;

  // Integracion condicional: no acumular si el error profundiza la saturacion.
  if (!((saturatedHigh && error > 0.0f) ||
        (saturatedLow && error < 0.0f))) {
    ctrlState.integral += deltaIntegral;
    if (ctrlCfg.integralLimit > 0.0f) {
      ctrlState.integral = constrain(ctrlState.integral,
                                     -ctrlCfg.integralLimit,
                                      ctrlCfg.integralLimit);
    }
  }

  ctrlState.prevError = error;
  return ctrlCfg.kp * error + ctrlState.integral;
}

// =============================================================
// SETUP / LOOP
// =============================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(sensorCfg.adcPin, INPUT);
  pinMode(ledPins.ok, OUTPUT);
  pinMode(ledPins.fault, OUTPUT);
  digitalWrite(ledPins.ok, LOW);
  digitalWrite(ledPins.fault, LOW);

  analogSetWidth(12);
  analogSetAttenuation(ADC_11db);

  setupPWM();

  // Duty inicial = nominal
  dutyCycle = pwmCfg.dutyNominal;
  applyDuty(dutyCycle);

  printConfig();
  Serial.println("Sistema listo.");
  Serial.println("Comandos serie:");
  Serial.println("  's' + valor -> cambiar setpoint (ej: s1.5)");
  Serial.println("  'd' + valor -> cambiar duty nominal (ej: d0.41)");
  Serial.println("  'i' -> imprimir estado actual");
  Serial.println();
}

void loop() {
  static uint32_t lastPrintUs = 0;

  if (Serial.available() > 0) {
    String cmd = Serial.readString();
    cmd.trim();

    if (cmd.length() > 0) {
      char c = cmd.charAt(0);
      float value = cmd.substring(1).toFloat();

      switch (c) {
        case 's':
        case 'S':
          ctrlCfg.setpointA = value;
          Serial.printf("Setpoint actualizado: %.3f A\n", ctrlCfg.setpointA);
          break;

        case 'd':
        case 'D':
          pwmCfg.dutyNominal = constrain(value, pwmCfg.dutyMin, pwmCfg.dutyMax);
          Serial.printf("Duty nominal actualizado: %.4f\n", pwmCfg.dutyNominal);
          break;

        case 'i':
        case 'I':
          Serial.printf("medicion=%.3f A, setpoint=%.3f A, error=%.3f A, duty=%.4f\n",
                        currentA, ctrlCfg.setpointA, errorA, dutyCycle);
          break;

        default:
          break;
      }
    }
  }

  uint32_t nowUs = micros();
  uint32_t samplePeriodUs = (uint32_t)(ctrlCfg.Ts * 1e6f);

  if ((nowUs - lastSampleUs) >= samplePeriodUs) {
    lastSampleUs += samplePeriodUs;

    currentA = readCurrentA();
    errorA = ctrlCfg.setpointA - currentA;

    float controlAction = computeControlAction(errorA);

    // Duty nominal + accion correctiva del controlador PI.
    float dutyCandidate = pwmCfg.dutyNominal + controlAction;
    dutyCandidate = constrain(dutyCandidate, pwmCfg.dutyMin, pwmCfg.dutyMax);

    applyDuty(dutyCandidate);

    if ((nowUs - lastPrintUs) >= 200000UL) {
      lastPrintUs = nowUs;
      Serial.printf("medicion=%.3f A, setpoint=%.3f A, error=%.3f A, duty=%.4f\n",
                    currentA, ctrlCfg.setpointA, errorA, dutyCycle);
    }
  }
}
