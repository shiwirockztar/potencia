#include <Arduino.h>

// Configuración del canal PWM (LEDC de ESP32)
const int PWM_PIN    = 15;   // GPIO de salida
const int PWM_CHAN   = 0;    // Canal LEDC (0-15)
const int PWM_RES   = 12;   // Resolución en bits (4096 niveles)

int   freq = 1000;   // Hz
float duty = 50.0;   // %

void applyPWM() {
  ledcSetup(PWM_CHAN, freq, PWM_RES);
  ledcAttachPin(PWM_PIN, PWM_CHAN);
  uint32_t maxVal = (1 << PWM_RES) - 1;
  uint32_t dutyVal = (uint32_t)((duty / 100.0f) * maxVal);
  ledcWrite(PWM_CHAN, dutyVal);
}

void setup() {
  Serial.begin(115200);
  applyPWM();
  Serial.println("PWM Control Serial - ESP32");
  Serial.println("F=<frecuencia Hz>   (60 a 40000)");
  Serial.println("D=<duty %>          (0 a 100)");
  Serial.println("S                   (estado)");
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    String cmdUp = cmd;
    cmdUp.toUpperCase();

    if (cmdUp.startsWith("F=")) {
      int nuevaFreq = cmd.substring(2).toInt();
      if (nuevaFreq >= 60 && nuevaFreq <= 40000) {
        freq = nuevaFreq;
        applyPWM();
        Serial.print("Frecuencia: "); Serial.print(freq); Serial.println(" Hz");
      } else {
        Serial.println("Error: rango 60-40000 Hz");
      }

    } else if (cmdUp.startsWith("D=")) {
      float nuevoDuty = cmd.substring(2).toFloat();
      if (nuevoDuty >= 0 && nuevoDuty <= 100) {
        duty = nuevoDuty;
        applyPWM();
        Serial.print("Duty: "); Serial.print(duty); Serial.println(" %");
      } else {
        Serial.println("Error: rango 0-100 %");
      }

    } else if (cmdUp == "S") {
      Serial.print("Frecuencia: "); Serial.print(freq); Serial.println(" Hz");
      Serial.print("Duty: "); Serial.print(duty); Serial.println(" %");

    } else {
      Serial.println("Comando no reconocido");
    }
  }
}