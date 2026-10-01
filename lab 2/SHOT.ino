/*
  Control de fase para SCR con ESP32
  Red: 60 Hz
  Entrada: pulso de cruce por cero
  Salida: pulso para activar Q1 / MOC3020

  Escribir un ángulo entre 0 y 180 en el Monitor Serial.
*/

const int ZERO_CROSS_PIN = 27;  // Entrada desde detección de cruce por cero
const int GATE_PIN = 26;        // Salida hacia R4 -> Q1 -> MOC3020

const unsigned long HALF_CYCLE_US = 8333;  // Semiciclo para 60 Hz
const unsigned long PULSE_WIDTH_US = 500;  // Ancho inicial del pulso de disparo

volatile bool zeroCrossDetected = false;
volatile unsigned long lastZeroCrossUs = 0;

int alpha = 90;  // Ángulo de disparo inicial en grados

void IRAM_ATTR zeroCrossISR() {
  unsigned long now = micros();

  // Antirrebote/filtro básico: evita dobles detecciones muy cercanas
  if (now - lastZeroCrossUs > 3000) {
    zeroCrossDetected = true;
    lastZeroCrossUs = now;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(ZERO_CROSS_PIN, INPUT);
  pinMode(GATE_PIN, OUTPUT);

  digitalWrite(GATE_PIN, LOW);

  attachInterrupt(
    digitalPinToInterrupt(ZERO_CROSS_PIN),
    zeroCrossISR,
    RISING
  );

  Serial.println("Control de fase SCR listo.");
  Serial.println("Escriba un angulo entre 0 y 180 y presione Enter.");
  Serial.println("Ejemplo: 90");
}

void loop() {
  // Leer nuevo ángulo desde el monitor serial
  if (Serial.available() > 0) {
    int nuevoAlpha = Serial.parseInt();

    if (nuevoAlpha >= 0 && nuevoAlpha <= 180) {
      alpha = nuevoAlpha;

      Serial.print("Angulo configurado: ");
      Serial.print(alpha);
      Serial.println(" grados");
    } else {
      Serial.println("Error: ingrese un valor entero entre 0 y 180.");
    }

    // Elimina saltos de línea pendientes en el buffer serial
    while (Serial.available() > 0) {
      Serial.read();
    }
  }

  // Procesar cada cruce por cero detectado
  if (zeroCrossDetected) {
    noInterrupts();
    zeroCrossDetected = false;
    interrupts();

    // Evita disparar si alpha es 180°, equivalente a potencia mínima.
    if (alpha < 180) {
      unsigned long delayUs = map(alpha, 0, 180, 0, HALF_CYCLE_US);

      // Evita llegar exactamente al final del semiciclo
      if (delayUs > HALF_CYCLE_US - PULSE_WIDTH_US) {
        delayUs = HALF_CYCLE_US - PULSE_WIDTH_US;
      }

      delayMicroseconds(delayUs);

      // Pulso que activa Q1 y, por ende, el LED del MOC3020
      digitalWrite(GATE_PIN, HIGH);
      delayMicroseconds(PULSE_WIDTH_US);
      digitalWrite(GATE_PIN, LOW);
    }
  }
}