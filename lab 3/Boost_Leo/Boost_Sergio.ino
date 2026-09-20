/* =======================================================================
   CONTROL DE CORRIENTE - BOOST CONVERTER - ESP32
   =======================================================================
   Lazo: setpoint (Iref) -> PI digital -> duty PWM -> boost -> ACS712 -> ADC
   Compensador continuo de referencia:  C(s) = Kp + Ki/s
       Kp = 0.4912
       Ki = 750.2   [1/s]
   Disenado para: fc ~ 790 Hz, PM ~ 54 deg, OS ~ 9.7 %, ts ~ 2 ms
   (equivalente en dinamica al tipo-II analogico, sin el polo de alta
   frecuencia: en digital ese filtrado se logra muestreando el ADC
   siempre en el mismo instante del ciclo PWM, no con un polo extra).

   HARDWARE:
   - ACS712 alimentado a 5V, salida centrada en 2.5V, sensibilidad 185mV/A
   - Salida del ACS712 -> DIVISOR RESISTIVO (ratio ~0.6, ej. R_top=10k,
     R_bot=15k) -> pin ADC del ESP32 (0-3.3V max)
   - Salida PWM (GPIO_PWM) -> driver UCC21520 -> gate del MOSFET del boost
   ======================================================================= */

#include <Arduino.h>

// ---------------------- CONFIGURACION DE PINES --------------------------
const int PIN_ADC_CORRIENTE = 34;   // pin ADC1 (34-39 son solo entrada)
const int PIN_PWM           = 25;   // salida PWM hacia el driver
const int LEDC_CHANNEL       = 0;
const int LEDC_RESOLUTION_BITS = 10;            // 0..1023
const int LEDC_MAX_DUTY      = (1 << LEDC_RESOLUTION_BITS) - 1;
const uint32_t FSW_HZ        = 20000;           // frecuencia de conmutacion

// ---------------------- ESCALADO DEL SENSOR ------------------------------
// Divisor resistivo entre ACS712 (0-5V) y el ADC del ESP32 (0-3.3V)
const float DIVISOR_RATIO   = 0.6f;    // Vadc = Vacs * DIVISOR_RATIO
const float ACS712_SENS     = 0.185f;  // V/A  (version 5A)
const float ACS712_VCC      = 5.0f;    // alimentacion del ACS712
float acs712_offset_V       = ACS712_VCC / 2.0f;  // se puede recalibrar en setup()

// ---------------------- LAZO DE CONTROL ----------------------------------
const float CONTROL_FS_HZ = 10000.0f;      // frecuencia del lazo digital
const float TS            = 1.0f / CONTROL_FS_HZ;

const float KP = 0.4912f;
const float KI = 750.2f;      // [1/s]

// setpoint de corriente [A] -- CAMBIALO AQUI o por Serial en caliente
volatile float Iref = 1.5f;
float Iref_objetivo = 1.5f;   // valor final (para el soft-start)
const float RAMPA_A_POR_S = 5.0f;   // A/s -> suaviza el arranque (evita saturar)

// ---------------------- VARIABLES DEL PI ----------------------------------
static float integrador = 0.0f;   // acumulador del termino integral
static float e_prev     = 0.0f;
static float duty_cmd   = 0.0f;   // salida del compensador, 0..1

// ---------------------- TIMER DE HARDWARE ----------------------------------
hw_timer_t *timerControl = NULL;
volatile bool bandera_control = false;

void IRAM_ATTR onTimerControl() {
  bandera_control = true;
}

// ---------------------- LECTURA DE CORRIENTE --------------------------------
float leerCorrienteA() {
  // analogReadMilliVolts ya aplica la calibracion interna del ADC del ESP32
  float v_adc_mV = analogReadMilliVolts(PIN_ADC_CORRIENTE);
  float v_adc    = v_adc_mV / 1000.0f;
  float v_acs    = v_adc / DIVISOR_RATIO;          // deshace el divisor
  float corriente = (v_acs - acs712_offset_V) / ACS712_SENS;
  return corriente;
}

// ---------------------- CALIBRACION DE OFFSET (opcional) --------------------
void calibrarOffsetACS712(int muestras = 200) {
  // Llamar SOLO si en ese instante estas seguro que iL ~ 0 A
  // (por ejemplo, antes de habilitar el PWM)
  double acc = 0;
  for (int i = 0; i < muestras; i++) {
    float v_adc_mV = analogReadMilliVolts(PIN_ADC_CORRIENTE);
    acc += (v_adc_mV / 1000.0f) / DIVISOR_RATIO;
    delay(1);
  }
  acs712_offset_V = acc / muestras;
  Serial.print("Offset ACS712 calibrado: ");
  Serial.println(acs712_offset_V, 4);
}

// ---------------------- COMPENSADOR PI CON ANTI-WINDUP -----------------------
float compensadorPI(float error) {
  // Integracion trapezoidal (Tustin):
  //   integrador += Ki * Ts/2 * (e[n] + e[n-1])
  float delta_int = KI * TS * 0.5f * (error + e_prev);

  float salida_sin_sat = KP * error + integrador + delta_int;

  // Anti-windup por integracion condicional:
  // solo acumula si no estamos saturados, o si el error empuja
  // a salir de la saturacion (no a profundizarla)
  bool saturado_alto = (salida_sin_sat > 1.0f);
  bool saturado_bajo = (salida_sin_sat < 0.0f);

  if (!( (saturado_alto && error > 0) || (saturado_bajo && error < 0) )) {
    integrador += delta_int;
  }

  e_prev = error;

  float salida = KP * error + integrador;
  if (salida > 1.0f) salida = 1.0f;
  if (salida < 0.0f) salida = 0.0f;
  return salida;
}


void setup() {
  Serial.begin(115200);
  delay(200);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_ADC_CORRIENTE, ADC_11db); // rango ~0-3.3V

  ledcSetup(LEDC_CHANNEL, FSW_HZ, LEDC_RESOLUTION_BITS);
  ledcAttachPin(PIN_PWM, LEDC_CHANNEL);
  ledcWrite(LEDC_CHANNEL, 0);  

  // Descomenta esta linea SOLO si en el arranque iL=0A garantizado
  // calibrarOffsetACS712();

  // Timer de hardware -> dispara el lazo de control a CONTROL_FS_HZ
  timerControl = timerBegin(0, 80, true);        // prescaler 80 -> tick = 1us (clk 80MHz)
  timerAttachInterrupt(timerControl, &onTimerControl, true);
  uint64_t periodo_us = (uint64_t)(1e6f / CONTROL_FS_HZ);
  timerAlarmWrite(timerControl, periodo_us, true);
  timerAlarmEnable(timerControl);

  Serial.println("Control de corriente iniciado.");
  Serial.println("Escribe un numero + Enter para cambiar el setpoint (A).");
}

// ---------------------- LOOP PRINCIPAL -----------------------------------------
void loop() {
  // --- setpoint por Serial (opcional, util para pruebas) ---
  if (Serial.available()) {
    float nuevo = Serial.parseFloat();
    if (nuevo > 0.0f && nuevo < 5.0f) {
      Iref_objetivo = nuevo;
      Serial.print("Nuevo setpoint: ");
      Serial.println(Iref_objetivo, 3);
    }
    while (Serial.available()) Serial.read();  // limpia el buffer
  }

  if (bandera_control) {
    bandera_control = false;

    // --- soft-start / rampa del setpoint (evita el pico de arranque) ---
    float paso = RAMPA_A_POR_S * TS;
    if (Iref < Iref_objetivo)      Iref = min(Iref + paso, Iref_objetivo);
    else if (Iref > Iref_objetivo) Iref = max(Iref - paso, Iref_objetivo);

    // --- lazo de control ---
    float iL_medida = leerCorrienteA();
    float error     = Iref - iL_medida;
    duty_cmd        = compensadorPI(error);

    uint32_t cuenta_pwm = (uint32_t)(duty_cmd * LEDC_MAX_DUTY);
    ledcWrite(LEDC_CHANNEL, cuenta_pwm);

    // --- telemetria opcional (comenta si te sobra tiempo de CPU) ---
    static int contador_print = 0;
    if (++contador_print >= 500) {         // imprime ~20 veces/seg a 10kHz
      contador_print = 0;
      Serial.print("Iref="); Serial.print(Iref, 3);
      Serial.print("  iL=");  Serial.print(iL_medida, 3);
      Serial.print("  D=");   Serial.println(duty_cmd, 4);
    }
  }
}