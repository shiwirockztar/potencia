#include <Arduino.h>

const uint8_t PIN_NORMAL = 25, PIN_INVERTIDO = 26;
uint32_t frecuencia = 60;
uint8_t duty = 50;

void actualizarPWM() {
  ledcAttach(PIN_NORMAL, frecuencia, 10);
  ledcAttach(PIN_INVERTIDO, frecuencia, 10);
  ledcOutputInvert(PIN_INVERTIDO, true);
  ledcWrite(PIN_NORMAL, 1023 * duty / 100);
  ledcWrite(PIN_INVERTIDO, 1023 * duty / 100);
}

void setup() {
  Serial.begin(115200);
  actualizarPWM();
  Serial.println("F<60..20000> para frecuencia, D<0..100> para duty");
}

void loop() {
  if (Serial.available()) {
    char comando = Serial.read();
    uint32_t valor = Serial.parseInt();
    if (comando == 'F' || comando == 'f') frecuencia = constrain(valor, 60, 20000);
    if (comando == 'D' || comando == 'd') duty = constrain(valor, 0, 100);
    actualizarPWM();
  }
}