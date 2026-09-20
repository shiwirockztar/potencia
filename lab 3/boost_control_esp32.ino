/*
 * ================================================================
 * BOOST CONVERTER — CONTROL DE CORRIENTE CON ESP32
 * ================================================================
 * Compensador Tipo III en cascada (bilineal/Tustin, Ts = 100 µs)
 * PWM de conmutación:  20 kHz (hardware LEDC — siempre preciso)
 * Frecuencia de control: 10 kHz (Ts = 100 µs — da tiempo al ADC)
 *
 * Función de transferencia del compensador:
 *   C(s) = Kc · (s+wz1)·(s+wz2) / [s·(s+wp1)·(s+wp2)]
 *   Kc = 15 255.1538
 *   wz1 = 2 000  rad/s,  wz2 = 8 000  rad/s
 *   wp1 = 60 000 rad/s,  wp2 = 100 000 rad/s
 *
 * Sistema diseñado:
 *   Vin = 10 V,  Vout ≈ 17 V,  P = 15 W
 *   IL_ref = 1.5 A,  Fsw = 20 kHz
 *   Settling ≤ 10 ms,  error estacionario = 0
 *
 * ================================================================
 * CONEXIONES HARDWARE
 * ================================================================
 *
 *   GPIO25 ────────────────────────→  UCC21520 pin INA   [PWM 20 kHz]
 *
 *   ACS712-05B                        ESP32
 *      Out ──[R1 = 10 kΩ]──┬──────→  GPIO34  (ADC1_CH6, solo entrada)
 *                          [R2 = 15 kΩ]
 *                          │
 *                         GND
 *
 *   GPIO2  →  R = 330 Ω  →  LED verde (anode)  →  GND    [Estado OK]
 *   GPIO4  →  R = 330 Ω  →  LED rojo  (anode)  →  GND    [Falla]
 *
 * IMPORTANTE:
 *   • GND del ESP32 = GND de la etapa de Control (nodo común)
 *   • Alimentar el ESP32 con regulador 3.3 V INDEPENDIENTE de los 10 V
 *   • El ACS712 se alimenta con los 5 V del circuito de control
 *   • Si VD_RATIO = 1.0 (sin divisor): V_max en GPIO34 ≈ 3.15 V con
 *     I = 3.5 A → dentro del rango con ADC_11db, pero el divisor
 *     ofrece mayor margen de seguridad y se recomienda.
 *
 * CALIBRACIÓN DEL OFFSET ADC:
 *   1. Con IL = 0 A, medir la tensión en GPIO34 con voltímetro.
 *   2. Valor teórico: V_GPIO34 = 2.5 × VD = 1.5 V
 *   3. Ajustar:  ADC_OFFSET_V = 1.5 - V_medida
 *
 * ================================================================
 * NOTAS DE DISEÑO
 * ================================================================
 * El compensador fue factorizado en tres etapas en cascada:
 *   H1(s) = (s+wz1)/s         — integrador + cero
 *   H2(s) = (s+wz2)/(s+wp1)  — lead-lag
 *   H3(s) = 1/(s+wp2)         — polo de alta frecuencia
 *
 * Discretización bilineal (Tustin, p = 2/Ts = 20 000 rad/s):
 *   Etapa 1:  y1[n] = y1[n-1] + 1.1·e[n] - 0.9·e[n-1]
 *   Etapa 2:  y2[n] = -0.5·y2[n-1] + 0.35·y1[n] - 0.15·y1[n-1]
 *   Etapa 3:  y3[n] = -0.6667·y3[n-1] + (1/120000)·(y2[n]+y2[n-1])
 *   Salida:   d[n]  = Kc · y3[n]   (clamp a [D_MIN, D_MAX])
 *
 * Anti-windup: si d[n] satura, se congela el integrador (Etapa 1).
 * Inicialización bumpless: al finalizar el soft-start, los estados
 * se pre-cargan con los valores de régimen permanente (d = D_NOM, e = 0)
 * para evitar un escalón de duty al activar el compensador.
 * ================================================================
 */

#include <Arduino.h>

// ════════════════════════════════════════════════════════════════
// PINES
// ════════════════════════════════════════════════════════════════
constexpr int PIN_ADC    = 34;   // ACS712 → ADC1_CH6 (GPIO solo entrada)
constexpr int PIN_PWM    = 25;   // PWM → UCC21520 pin INA
constexpr int PIN_LED_OK =  2;   // LED verde  (operación normal)
constexpr int PIN_LED_FL =  4;   // LED rojo   (falla activa)

// ════════════════════════════════════════════════════════════════
// CONFIGURACIÓN PWM (LEDC hardware)
// ════════════════════════════════════════════════════════════════
constexpr int      LEDC_CH    = 0;
constexpr int      PWM_FREQ   = 20000;            // 20 kHz
constexpr int      PWM_BITS   = 10;               // Resolución: 1 024 cuentas
constexpr uint32_t PWM_MAX    = (1u << PWM_BITS) - 1;  // 1023

// ════════════════════════════════════════════════════════════════
// PARÁMETROS DEL CONVERTIDOR
// ════════════════════════════════════════════════════════════════
constexpr float IL_REF   = 1.5f;     // Referencia de corriente inductora [A]
constexpr float D_NOM    = 0.4117f;  // Duty nominal  (D = 1 - Vin/Vout)
constexpr float D_MAX    = 0.85f;    // Duty máximo (proteger MOSFET/diodo)
constexpr float D_MIN    = 0.02f;    // Duty mínimo
constexpr float I_TRIP   = 3.5f;     // Disparo duro — apagado inmediato [A]
constexpr float I_WARN   = 2.5f;     // Umbral suave — reducción de duty [A]

// ════════════════════════════════════════════════════════════════
// SENSOR ACS712-05B + DIVISOR DE TENSIÓN
// ════════════════════════════════════════════════════════════════
// Característica: V_ACS = Vcc/2 + I × 0.185 V/A  (Vcc = 5 V)
// → a I = 0 A:  V_ACS = 2.5 V
// → a I = 1.5A: V_ACS = 2.5 + 1.5×0.185 = 2.7775 V
// → a I = 3.5A: V_ACS = 2.5 + 3.5×0.185 = 3.1475 V (max seguro)
//
// Divisor R1=10 kΩ (serie) / R2=15 kΩ (a GND):
//   VD_RATIO = R2/(R1+R2) = 15/25 = 0.6
//   V_GPIO34 = V_ACS × 0.6   →   máx. 1.89 V con I_TRIP (seguro)
//
// Para operar SIN divisor: cambiar VD_RATIO = 1.0f
constexpr float ACS_VCC   = 5.0f;
constexpr float ACS_SENS  = 0.185f;              // V/A  (ACS712-05B)
constexpr float ACS_OFF   = ACS_VCC / 2.0f;     // 2.5 V a I = 0
constexpr float VD_RATIO  = 15.0f / (10.0f + 15.0f);  // 0.6

constexpr float ADC_VREF  = 3.3f;               // Ref. ADC del ESP32
constexpr float ADC_CNT   = 4095.0f;            // 2^12 - 1

// Offset de calibración [V] — ajustar si es necesario (ver instrucciones arriba)
constexpr float ADC_OFFSET_V = 0.0f;

// Filtro IIR de 1er orden para la corriente medida
// alpha = 0.5  →  τ ≈ 100 µs (1 sample a 10 kHz) — respuesta rápida con algo de filtrado
constexpr float IIR_ALPHA = 0.5f;

// ════════════════════════════════════════════════════════════════
// COMPENSADOR TIPO III — COEFICIENTES DISCRETOS (Tustin, Ts=100 µs)
// ════════════════════════════════════════════════════════════════
//
// p = 2/Ts = 20 000 rad/s  (parámetro de Tustin)
//
// ── Etapa 1: H1(s) = (s + wz1) / s   [wz1 = 2000 rad/s] ──────
//    H1(z) = [20000(z-1) + 2000(z+1)] / [20000(z-1)]
//           = 1.1·(z - 0.8182) / (z - 1)
//    → y1[n] = y1[n-1] + 1.1·e[n] - 0.9·e[n-1]
//
// ── Etapa 2: H2(s) = (s+wz2)/(s+wp1)  [wz2=8000, wp1=60000] ──
//    H2(z) = [28000z - 12000] / [80000z + 40000]
//           = 0.35·(z - 0.4286) / (z + 0.5)
//    → y2[n] = -0.5·y2[n-1] + 0.35·y1[n] - 0.15·y1[n-1]
//
// ── Etapa 3: H3(s) = 1/(s+wp2)        [wp2 = 100000 rad/s] ───
//    H3(z) = (z+1) / [120000z + 80000]
//           = (1/120000)·(z+1) / (z + 2/3)
//    → y3[n] = -0.6667·y3[n-1] + (1/120000)·(y2[n] + y2[n-1])
//
// ── Salida: d[n] = Kc · y3[n]  →  clamp a [D_MIN, D_MAX] ─────
//
constexpr float KC = 15255.1538f;

// Etapa 1
constexpr float S1_B0 =  1.1f;
constexpr float S1_B1 = -0.9f;
// Etapa 2
constexpr float S2_A1 = -0.5f;
constexpr float S2_B0 =  0.35f;
constexpr float S2_B1 = -0.15f;
// Etapa 3
constexpr float S3_A1 = -0.6667f;
constexpr float S3_SC =  1.0f / 120000.0f;

// ════════════════════════════════════════════════════════════════
// ESTADOS DE RÉGIMEN PERMANENTE (inicialización bumpless)
// Con error e = 0 y d = D_NOM en régimen, se verifica:
//   d  = Kc × y3_ss  →  y3_ss = D_NOM / Kc
//   y3_ss = H3_DC × y2_ss,  H3_DC = 1/wp2  →  y2_ss = y3_ss × wp2
//   y2_ss = H2_DC × y1_ss,  H2_DC = wz2/wp1 →  y1_ss = y2_ss × wp1/wz2
// ════════════════════════════════════════════════════════════════
constexpr float Y3_SS = D_NOM / KC;                          // ≈ 2.699e-5
constexpr float Y2_SS = Y3_SS * 100000.0f;                  // ≈ 2.699
constexpr float Y1_SS = Y2_SS * (60000.0f / 8000.0f);      // ≈ 20.24

// ════════════════════════════════════════════════════════════════
// SOFT-START
// ════════════════════════════════════════════════════════════════
constexpr uint32_t SS_MS = 200;      // Duración del soft-start [ms]
constexpr float    SS_D0 = D_MIN;    // Duty inicial del soft-start

// ════════════════════════════════════════════════════════════════
// VARIABLES GLOBALES
// ════════════════════════════════════════════════════════════════

// — Estados del compensador (solo accedidos desde loop(), nunca desde ISR) —
float y1p = 0.0f;    // y1[n-1]
float y2p = 0.0f;    // y2[n-1]
float y3p = 0.0f;    // y3[n-1]
float ep  = 0.0f;    // e[n-1]
float Iflt = 0.0f;   // Corriente filtrada por IIR
float Dapp = 0.0f;   // Último duty cycle aplicado

// — Compartida con ISR (debe ser volatile) —
volatile bool ctrl_tick = false;

// — Estado del sistema —
bool fault_on  = false;
bool softstart = true;
uint32_t ss_t0 = 0;   // millis() al inicio del soft-start

// — Timer hardware —
hw_timer_t*  htim = nullptr;
portMUX_TYPE hmux = portMUX_INITIALIZER_UNLOCKED;

// ════════════════════════════════════════════════════════════════
// ISR DEL TIMER — 10 kHz (cada 100 µs)
// ════════════════════════════════════════════════════════════════
void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&hmux);
  ctrl_tick = true;
  portEXIT_CRITICAL_ISR(&hmux);
}

// ════════════════════════════════════════════════════════════════
// readI(): Leer ACS712 con filtro IIR
//   Retorna la corriente INSTANTÁNEA (sin filtrar) — para protección dura.
//   Actualiza el global Iflt (corriente filtrada) — para el compensador.
// ════════════════════════════════════════════════════════════════
float readI() {
  int   raw   = analogRead(PIN_ADC);
  float Vadc  = (float)raw * (ADC_VREF / ADC_CNT) + ADC_OFFSET_V;
  float Vacs  = Vadc / VD_RATIO;              // Eliminar efecto divisor
  float Iraw  = (Vacs - ACS_OFF) / ACS_SENS; // V → A
  Iflt = IIR_ALPHA * Iraw + (1.0f - IIR_ALPHA) * Iflt;  // Filtro IIR
  return Iraw;  // Instantánea, sin filtrar
}

// ════════════════════════════════════════════════════════════════
// applyD(): Aplicar duty cycle (con clamp automático)
// ════════════════════════════════════════════════════════════════
void applyD(float d) {
  d    = constrain(d, D_MIN, D_MAX);
  Dapp = d;
  ledcWrite(LEDC_CH, (uint32_t)(d * (float)PWM_MAX));
}

// ════════════════════════════════════════════════════════════════
// trigFault(): Activar falla — apaga PWM, enciende LED rojo
// ════════════════════════════════════════════════════════════════
void trigFault(const char* msg) {
  fault_on = true;
  ledcWrite(LEDC_CH, 0);
  Dapp = 0.0f;
  digitalWrite(PIN_LED_OK, LOW);
  digitalWrite(PIN_LED_FL, HIGH);
  // Resetear estados del compensador a cero
  y1p = 0.0f; y2p = 0.0f; y3p = 0.0f; ep = 0.0f;
  Serial.print("[FALLA] ");
  Serial.println(msg);
}

// ════════════════════════════════════════════════════════════════
// initComp(): Inicializar compensador
//   bumpless = true  →  pre-cargar estados de régimen (d = D_NOM, e = 0)
//   bumpless = false →  partir de cero
// ════════════════════════════════════════════════════════════════
void initComp(bool bumpless) {
  if (bumpless) {
    y1p = Y1_SS;  // ≈ 20.24
    y2p = Y2_SS;  // ≈ 2.699
    y3p = Y3_SS;  // ≈ 2.70e-5
  } else {
    y1p = 0.0f; y2p = 0.0f; y3p = 0.0f;
  }
  ep = 0.0f;
}

// ════════════════════════════════════════════════════════════════
// compStep(): Un paso del compensador Tipo III
//   Im: corriente medida filtrada [A]
//   Retorna: duty cycle calculado (ya con anti-windup aplicado)
// ════════════════════════════════════════════════════════════════
float compStep(float Im) {
  float err = IL_REF - Im;   // e[n] = I_ref - I_medida

  // ── Etapa 1: integrador + cero wz1 ─────────────────────────
  float y1 = y1p + S1_B0 * err + S1_B1 * ep;

  // ── Etapa 2: lead-lag (wz2 / wp1) ──────────────────────────
  float y2 = S2_A1 * y2p + S2_B0 * y1 + S2_B1 * y1p;

  // ── Etapa 3: polo de alta frecuencia (wp2) ──────────────────
  float y3 = S3_A1 * y3p + S3_SC * (y2 + y2p);

  // ── Calcular duty crudo ─────────────────────────────────────
  float d = KC * y3;

  // ── Anti-windup: si d satura, congelar integrador ───────────
  // Si el duty satura (> D_MAX o < D_MIN), congelamos y1 (la etapa
  // integradora) y recomputamos y2, y3 con y1 congelado. Esto evita
  // que el integrador acumule error durante saturación.
  if (d > D_MAX || d < D_MIN) {
    y1 = y1p;   // Congelar: no actualizar el integrador
    y2 = S2_A1 * y2p + S2_B0 * y1 + S2_B1 * y1p;
    y3 = S3_A1 * y3p + S3_SC * (y2 + y2p);
    d  = constrain(KC * y3, D_MIN, D_MAX);  // Clamp final de seguridad
  }

  // ── Actualizar estados para el próximo ciclo ─────────────────
  y1p = y1;
  y2p = y2;
  y3p = y3;
  ep  = err;

  return d;
}

// ════════════════════════════════════════════════════════════════
// SETUP
// ════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println(F("============================================"));
  Serial.println(F("  BOOST CONVERTER - Compensador Tipo III   "));
  Serial.println(F("============================================"));
  Serial.printf ("  IL_ref  = %.2f A\n",   IL_REF);
  Serial.printf ("  D_nom   = %.4f\n",     D_NOM);
  Serial.printf ("  Fsw PWM = %d Hz  (LEDC hardware)\n", PWM_FREQ);
  Serial.printf ("  Fctrl   = 10 000 Hz  (Ts = 100 µs)\n");
  Serial.printf ("  I_trip  = %.1f A  |  I_warn = %.1f A\n", I_TRIP, I_WARN);
  Serial.printf ("  D_min   = %.2f   |  D_max  = %.2f\n",    D_MIN,  D_MAX);
  Serial.printf ("  VD_ratio = %.2f  |  ACS_sens = %.0f mV/A\n",
                 VD_RATIO, ACS_SENS * 1000.0f);
  Serial.println(F("--------------------------------------------"));
  Serial.printf ("  Y1_ss = %.4f | Y2_ss = %.5f | Y3_ss = %.3e\n",
                 Y1_SS, Y2_SS, Y3_SS);
  Serial.println(F("--------------------------------------------\n"));

  // ── GPIOs ────────────────────────────────────────────────────
  pinMode(PIN_LED_OK, OUTPUT);
  pinMode(PIN_LED_FL, OUTPUT);
  digitalWrite(PIN_LED_OK, LOW);
  digitalWrite(PIN_LED_FL, LOW);

  // ── ADC: GPIO34 = ADC1_CH6 ───────────────────────────────────
  analogSetWidth(12);              // 12 bits → 0 a 4095
  analogSetAttenuation(ADC_11db); // Rango ≈ 0–3.55 V (mejor para ~3.3 V max)

  // ── PWM LEDC: 20 kHz, 10 bits ─────────────────────────────────
  ledcSetup(LEDC_CH, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_PWM, LEDC_CH);
  ledcWrite(LEDC_CH, 0);   // Empezar con duty = 0 (MOSFET apagado)

  // ── Compensador e IIR: iniciar en cero ───────────────────────
  initComp(false);
  Iflt = 0.0f;
  Dapp = 0.0f;

  // ── Timer hardware → 10 kHz (Ts = 100 µs) ───────────────────
  //   Prescaler = 8  →  f_timer = 80 MHz / 8 = 10 MHz
  //   Alarma    = 1000 →  T = 1000 / 10 MHz = 100 µs  →  10 kHz
  htim = timerBegin(0, 8, true);           // Timer 0, prescaler 8, count up
  timerAttachInterrupt(htim, &onTimer, true); // ISR en flanco ascendente
  timerAlarmWrite(htim, 1000, true);       // Alarma cada 1000 cuentas = 100 µs
  timerAlarmEnable(htim);

  // ── Soft-start ────────────────────────────────────────────────
  fault_on  = false;
  softstart = true;
  ss_t0     = millis();

  digitalWrite(PIN_LED_OK, HIGH);
  Serial.println(F("Sistema listo. Iniciando soft-start (200 ms)..."));
  Serial.println(F("Comandos serie: 'r' = reset falla  |  'i' = info\n"));
}

// ════════════════════════════════════════════════════════════════
// LOOP PRINCIPAL
// ════════════════════════════════════════════════════════════════
void loop() {
  static uint32_t t_print  = 0;
  static uint32_t n_cycles = 0;

  // ── Comandos por Serial ──────────────────────────────────────
  if (Serial.available()) {
    char c = (char)toupper((int)Serial.read());

    if (c == 'R') {
      // Reset de falla: reiniciar soft-start
      fault_on  = false;
      softstart = true;
      ss_t0     = millis();
      initComp(false);
      Iflt = 0.0f;
      Dapp = 0.0f;
      ledcWrite(LEDC_CH, 0);
      digitalWrite(PIN_LED_OK, HIGH);
      digitalWrite(PIN_LED_FL, LOW);
      Serial.println(F(">> Falla reseteada. Reiniciando soft-start..."));
    }
    else if (c == 'I') {
      Serial.println(F("\n── ESTADO ACTUAL ───────────────────────"));
      Serial.printf ("  I_filt  = %.3f A  (ref = %.3f A)\n", Iflt, IL_REF);
      Serial.printf ("  Error   = %.4f A\n", IL_REF - Iflt);
      Serial.printf ("  D_aplic = %.4f    (nom = %.4f)\n", Dapp, D_NOM);
      Serial.printf ("  y1 = %.4f  y2 = %.5f  y3 = %.4e\n", y1p, y2p, y3p);
      Serial.printf ("  Falla: %-3s  |  Softstart: %s\n",
                     fault_on  ? "SI" : "No",
                     softstart ? "SI" : "No");
      Serial.println(F("────────────────────────────────────────\n"));
    }
  }

  // ── Verificar tick del timer de control ──────────────────────
  bool run = false;
  portENTER_CRITICAL(&hmux);
  if (ctrl_tick) { ctrl_tick = false; run = true; }
  portEXIT_CRITICAL(&hmux);
  if (!run) return;  // Todavía no es tiempo de correr control
  n_cycles++;

  // ── Si hay falla activa: mantener PWM apagado ────────────────
  if (fault_on) {
    ledcWrite(LEDC_CH, 0);
    return;
  }

  // ── Leer corriente ACS712 ────────────────────────────────────
  // readI() actualiza Iflt (filtrado) y devuelve el valor instantáneo
  float Iraw = readI();

  // ── Protección dura: disparo instantáneo por sobrecorriente ──
  // Se usa el valor RAW (sin filtrar) para máxima velocidad de respuesta
  if (Iraw > I_TRIP) {
    trigFault("SOBRECORRIENTE instantanea > 3.5 A — PWM apagado");
    return;
  }

  // ── Soft-start: rampa lineal D_MIN → D_NOM en SS_MS ms ──────
  if (softstart) {
    uint32_t elapsed = millis() - ss_t0;

    if (elapsed < SS_MS) {
      // Rampa de duty cycle
      float t = (float)elapsed / (float)SS_MS;   // 0.0 → 1.0
      applyD(SS_D0 + (D_NOM - SS_D0) * t);
      return;   // No correr compensador durante soft-start
    }

    // ── Soft-start terminado: transición bumpless ────────────
    softstart = false;
    initComp(true);   // Pre-cargar estados de régimen (d ≈ D_NOM, e = 0)
    Iflt = Iraw;      // Sincronizar filtro IIR con lectura actual
    Dapp = D_NOM;     // Registrar duty nominal como punto de partida
    Serial.printf(">> Soft-start OK. I = %.3f A. Compensador Tipo III activo.\n",
                  Iraw);
  }

  // ── Protección suave: I_WARN < I_filt < I_TRIP ───────────────
  // Si la corriente filtrada supera I_WARN, reducir duty gradualmente
  // (0.2 % por ciclo → ≈ vaciado completo en 40 ms si se mantiene)
  if (Iflt > I_WARN) {
    applyD(Dapp - 0.002f);
    ep = 0.0f;   // Congelar e_prev para evitar un salto brusco al salir
    return;
  }

  // ── Control normal: un paso del compensador Tipo III ─────────
  float d_new = compStep(Iflt);
  applyD(d_new);

  // ── Telemetría por Serial cada 200 ms ────────────────────────
  if (millis() - t_print >= 200UL) {
    t_print = millis();
    Serial.printf("I=%.3fA  e=%.4fA  D=%.4f  y3=%.3e  [%u cyc/200ms]\n",
                  Iflt, IL_REF - Iflt, Dapp, y3p, n_cycles);
    n_cycles = 0;
  }
}

/*
 * ================================================================
 * GUÍA DE PUESTA EN MARCHA
 * ================================================================
 *
 * 1. CABLEADO:
 *    a. Conectar GND del ESP32 al GND del circuito de control.
 *    b. Alimentar ESP32 con regulador 3.3 V independiente.
 *    c. Divisor de tensión R1=10kΩ / R2=15kΩ entre ACS712(Out) y GPIO34.
 *    d. GPIO25 → UCC21520 pin INA (señal PWM 20 kHz).
 *    e. LEDs con resistencias 330 Ω en GPIO2 (verde) y GPIO4 (rojo).
 *
 * 2. CALIBRACIÓN DEL SENSOR:
 *    a. Con IL = 0 A (circuito de potencia apagado), medir V en GPIO34.
 *    b. Calcular: ADC_OFFSET_V = 1.5 - V_medida  [en voltios].
 *    c. Actualizar la constante ADC_OFFSET_V y reflashear.
 *
 * 3. PRIMERA PRUEBA (con carga resistiva, no inductiva):
 *    a. Abrir el Monitor Serie a 115200 baud.
 *    b. Energizar el circuito (fuente 10 V).
 *    c. El soft-start dura 200 ms (duty sube de 2 % a 41.17 %).
 *    d. El compensador se activa solo al finalizar el soft-start.
 *    e. Observar la telemetría: I debe converger a 1.5 A.
 *    f. Enviar 'i' para ver el estado interno del compensador.
 *
 * 4. AJUSTE DE Kc:
 *    Si la corriente oscila: reducir KC a ~0.5 × KC_actual.
 *    Si la corriente tarda demasiado: aumentar KC hasta ~2 × KC_actual.
 *    El valor KC = 15255.15 es el diseñado teóricamente.
 *
 * 5. FAULT RESET:
 *    Enviar 'r' por el Monitor Serie para reiniciar tras una falla.
 *
 * ================================================================
 * TABLA DE RESUMEN DE PROTECCIONES
 * ================================================================
 *  I > 3.5 A (raw)  → Apagado INMEDIATO del PWM (GPIO25 = 0)
 *  I > 2.5 A (filt) → Reducción gradual del duty (-0.2 % por ciclo)
 *  D > 0.85         → Clamp interno (MOSFET/diodo protegidos)
 *  D < 0.02         → Clamp interno
 * ================================================================
 */
