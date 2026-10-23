#include <Arduino.h>
#include <math.h>

const uint8_t PIN_A = 25, PIN_B = 26;
const uint32_t PORTADORA = 10000, FUNDAMENTAL = 50;
const uint8_t RESOLUCION = 11;
const uint16_t PASOS = PORTADORA / FUNDAMENTAL;
const uint16_t MAX_DUTY = (1 << RESOLUCION) - 1;
const float MODULACION = 1.0;

hw_timer_t *timer = nullptr;
volatile uint16_t paso = 0;

void IRAM_ATTR actualizarSPWM() {
  float seno = sinf(2 * PI * paso / PASOS) * MODULACION;
  ledcWrite(PIN_A, seno > 0 ? seno * MAX_DUTY : 0);
  ledcWrite(PIN_B, seno < 0 ? -seno * MAX_DUTY : 0);
  paso = (paso + 1) % PASOS;
}

void setup() {
  ledcAttach(PIN_A, PORTADORA, RESOLUCION);
  ledcAttach(PIN_B, PORTADORA, RESOLUCION);
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &actualizarSPWM);
  timerAlarm(timer, 1000000 / PORTADORA, true, 0);
}

void loop() {}