/* =======================================================================
   BOOST CONVERTER - ESP32

   DOS PWM INDEPENDIENTES

   PWM 1 - GENERADOR
   -----------------
   PIN_PWM_GENERADOR = GPIO 26

   Genera una onda cuadrada independiente.
   Frecuencia: 20 kHz

   El duty del generador representa Iref.

   Iref se genera automaticamente como una señal pulsada:

       1.0 A  durante 100 ms
       2.0 A  durante 100 ms
       1.0 A  durante 100 ms
       2.0 A  durante 100 ms
       ...

   Frecuencia de Iref = 5 Hz
   Periodo completo = 200 ms


   GPIO 28 - INDICADOR NIVEL IREF
   -------------------------------
   GPIO 28 = 0 -> Iref = 1.0 A
   GPIO 28 = 1 -> Iref = 2.0 A


   PWM 2 - CONTROL PI
   ------------------
   PIN_PWM_CONTROL = GPIO 25

   Es la salida REAL del controlador PI.

   GPIO 25 -> UCC21520 -> MOSFET


   =======================================================================
   FLUJO
   =======================================================================

   Iref PULSADA
        |
        +------------------> GPIO 28
        |                    0 = 1 A
        |                    1 = 2 A
        |
        v
   PWM GENERADOR
        |
        v
      Iref
        |
        v
   +-----------+
   |     PI    |
   +-----------+
        |
        | duty
        v
   PWM CONTROL
        |
        v
    UCC21520
        |
        v
     MOSFET
        |
        v
      BOOST
        |
        v
      ACS712
        |
        v
   I medida
        |
        +-------> PI


   ======================================================================= */

#include <Arduino.h>


// =======================================================================
// PINES
// =======================================================================

// PWM GENERADOR
const int PIN_PWM_GENERADOR = 26;


// PWM SALIDA DEL CONTROLADOR PI
const int PIN_PWM_CONTROL = 25;


// ADC ACS712
const int PIN_ADC_CORRIENTE = 34;


// Salida PWM de la tension filtrada
const int PIN_SALIDA_FILTRADA = 33;


// GPIO PARA INDICAR NIVEL DE IREF
const int PIN_IREF_DIGITAL = 27;


// =======================================================================
// CONFIGURACION PWM
// =======================================================================

const uint32_t FSW_HZ = 20000;

const int LEDC_RESOLUTION_BITS = 10;

const int LEDC_MAX_DUTY =
    (1 << LEDC_RESOLUTION_BITS) - 1;


// =======================================================================
// REFERENCIA IREF PULSADA
// =======================================================================

// Nivel bajo de Iref
const float IREF_LOW = 1.0f;


// Nivel alto de Iref
const float IREF_HIGH = 2.0f;


// Tiempo que permanece cada nivel
const uint32_t IREF_INTERVAL_MS = 20;


// Iref actual
float Iref = IREF_LOW;


// Estado de la señal pulsada
//
// false -> Iref LOW  = 1.0 A
// true  -> Iref HIGH = 2.0 A

bool estadoIref = false;


// Temporizador de Iref
unsigned long tiempoAnteriorIref = 0;


// =======================================================================
// CONVERSION IREF -> DUTY PWM GENERADOR
// =======================================================================
//
// El PWM generador representa:
//
// 0 %   -> 0 A
// 50 %  -> 1.5 A
// 100 % -> 3 A
//
// Por tanto:
//
// Duty = Iref / 3.0
//
// Para nuestra señal:
//
// Iref = 1.0 A -> 33.33 %
// Iref = 2.0 A -> 66.67 %


// Corriente maxima representada por el PWM generador
const float IREF_MAX_PWM = 3.0f;


// Duty actual del generador
float duty_generador = 0.0f;


// =======================================================================
// ACS712
// =======================================================================

// Divisor resistivo:
//
// ACS712 -> 10 kOhm -> GPIO34 -> 20 kOhm -> GND
//
// Vadc = Vacs * 20 / (10 + 20)

const float DIVISOR_RATIO =
    20.0f / (10.0f + 20.0f);


// Sensibilidad medida
const float ACS712_SENS = 0.1891f;


// Numero de muestras promediadas por lectura
const int NUM_MUESTRAS_CORRIENTE = 8;


// Numero de muestras para calibrar el cero
const int NUM_MUESTRAS_OFFSET = 128;


// Filtro EMA aplicado a la corriente medida
const float FILTRO_ALPHA = 0.05f;


// Tension maxima representada en GPIO33
const float TENSION_SALIDA_MAX = 3.3f;


// Offset medido en la salida del ACS712
float acs712_offset_V = 2.3853f;


float tension_adc_medido = 0.0f;


float tension_adc_filtrada = 0.0f;


// =======================================================================
// LIMITADOR DE CORRIENTE
// =======================================================================

const float IL_MAX = 2.0f;

const float KLIM = 4.0f;


// =======================================================================
// CONTROL PI
// =======================================================================

const float CONTROL_FS_HZ = 10000.0f;

const float TS =
    1.0f / CONTROL_FS_HZ;


const float KP = 0.4912f; //0.4912f

const float KI = 750.2f;


// =======================================================================
// VARIABLES PI
// =======================================================================

float integrador = 0.0f;

float e_prev = 0.0f;

float duty_control = 0.0f;


// =======================================================================
// TIMER
// =======================================================================

hw_timer_t *timerControl = NULL;

volatile bool bandera_control = false;


// =======================================================================
// INTERRUPCION
// =======================================================================

void IRAM_ATTR onTimerControl()
{
    bandera_control = true;
}


// =======================================================================
// LECTURA ACS712
// =======================================================================

float leerCorrienteA()
{
    float suma_adc_mV = 0.0f;


    for (
        int muestra = 0;
        muestra < NUM_MUESTRAS_CORRIENTE;
        muestra++
    )
    {
        suma_adc_mV +=
            analogReadMilliVolts(
                PIN_ADC_CORRIENTE
            );
    }


    float v_adc_mV =
        suma_adc_mV /
        NUM_MUESTRAS_CORRIENTE;


    float v_adc =
        v_adc_mV / 1000.0f;


    tension_adc_medido =
        v_adc;


    // Recuperar tension original del ACS712
    float v_acs =
        v_adc / DIVISOR_RATIO;F


    // Convertir tension a corriente
    float corriente =
        (v_acs - acs712_offset_V)
        / ACS712_SENS;


    return corriente;
}


// =======================================================================
// PI
// =======================================================================

float compensadorPI(float error)
{
    // Integracion trapezoidal
    float delta_int =
        KI * TS * 0.5f *
        (error + e_prev);


    float salida_sin_sat =
        KP * error +
        integrador +
        delta_int;


    // Saturaciones
    bool saturado_alto =
        salida_sin_sat > 1.0f;


    bool saturado_bajo =
        salida_sin_sat < 0.0f;


    // Anti-windup
    if (!(
        (saturado_alto && error > 0) ||
        (saturado_bajo && error < 0)
    ))
    {
        integrador += delta_int;
    }


    e_prev = error;


    // Salida
    float salida =
        KP * error +
        integrador;


    // Limitar
    if (salida > 1.0f)
        salida = 1.0f;


    if (salida < 0.0f)
        salida = 0.0f;


    return salida;
}


// =======================================================================
// ACTUALIZAR PWM GENERADOR
// =======================================================================

void actualizarPWMGenerador()
{
    // Conversion de Iref a duty.
    //
    // Iref = 1.0 A -> 33.33 %
    // Iref = 2.0 A -> 66.67 %

    duty_generador =
        Iref / IREF_MAX_PWM;


    // Limitar duty
    if (duty_generador > 1.0f)
        duty_generador = 1.0f;


    if (duty_generador < 0.0f)
        duty_generador = 0.0f;


    uint32_t cuenta_pwm =
        (uint32_t)
        (
            duty_generador *
            LEDC_MAX_DUTY
        );


    ledcWrite(
        PIN_PWM_GENERADOR,
        cuenta_pwm
    );
}


// =======================================================================
// ACTUALIZAR IREF PULSADA
// =======================================================================
//
// Señal:
//
//     2.0 A  ┌────────┐          ┌────────┐
//            │        │          │        │
//     1.0 A ─┘        └──────────┘        └──
//
//            100 ms    100 ms
//
// GPIO 28:
//
//     HIGH ──┐        ┌──────────┐        ┌──
//            │        │          │        │
//     LOW  ──┘────────┘          └────────
//
// Periodo completo = 200 ms
// Frecuencia = 5 Hz

void actualizarIref()
{
    unsigned long tiempoActual =
        millis();


    if (
        tiempoActual -
        tiempoAnteriorIref
        >= IREF_INTERVAL_MS
    )
    {
        tiempoAnteriorIref =
            tiempoActual;


        // Cambiar estado
        estadoIref =
            !estadoIref;


        // ---------------------------------------------------------------
        // NIVEL ALTO
        // Iref = 2 A
        // GPIO 28 = HIGH
        // ---------------------------------------------------------------

        if (estadoIref)
        {
            Iref =
                IREF_HIGH;


            digitalWrite(
                PIN_IREF_DIGITAL,
                HIGH
            );
        }


        // ---------------------------------------------------------------
        // NIVEL BAJO
        // Iref = 1 A
        // GPIO 28 = LOW
        // ---------------------------------------------------------------

        else
        {
            Iref =
                IREF_LOW;


            digitalWrite(
                PIN_IREF_DIGITAL,
                LOW
            );
        }


        // Actualizar PWM generador
        actualizarPWMGenerador();
    }
}


// =======================================================================
// SETUP
// =======================================================================

void setup()
{
    Serial.begin(115200);

    delay(500);


    // ===================================================================
    // GPIO 28 - INDICADOR DE IREF
    // ===================================================================

    pinMode(
        PIN_IREF_DIGITAL,
        OUTPUT
    );


    // Arranque en Iref = 1 A
    // Por tanto GPIO 28 = 0

    digitalWrite(
        PIN_IREF_DIGITAL,
        LOW
    );


    // ===================================================================
    // ADC
    // ===================================================================

    analogReadResolution(12);


    analogSetPinAttenuation(
        PIN_ADC_CORRIENTE,
        ADC_11db
    );


    // ===================================================================
    // CALIBRACION ACS712
    // ===================================================================

    // No debe circular corriente por el ACS712
    // durante este proceso.

    float suma_offset_mV = 0.0f;


    for (
        int muestra = 0;
        muestra < NUM_MUESTRAS_OFFSET;
        muestra++
    )
    {
        suma_offset_mV +=
            analogReadMilliVolts(
                PIN_ADC_CORRIENTE
            );
    }


    float offset_adc_V =
        (
            suma_offset_mV /
            NUM_MUESTRAS_OFFSET
        ) / 1000.0f;


    acs712_offset_V =
        offset_adc_V /
        DIVISOR_RATIO;


    Serial.print(
        "Offset ACS712 calibrado: "
    );

    Serial.print(
        acs712_offset_V,
        4
    );

    Serial.println(" V");


    // ===================================================================
    // PWM GENERADOR
    // ===================================================================

    ledcAttach(
        PIN_PWM_GENERADOR,
        FSW_HZ,
        LEDC_RESOLUTION_BITS
    );


    // Iniciar en 1.0 A
    Iref =
        IREF_LOW;


    estadoIref =
        false;


    // GPIO 28 = 0 porque Iref = 1 A

    digitalWrite(
        PIN_IREF_DIGITAL,
        LOW
    );


    actualizarPWMGenerador();


    // ===================================================================
    // PWM CONTROL PI
    // ===================================================================

    ledcAttach(
        PIN_PWM_CONTROL,
        FSW_HZ,
        LEDC_RESOLUTION_BITS
    );


    // PWM proporcional a la corriente filtrada
    ledcAttach(
        PIN_SALIDA_FILTRADA,
        FSW_HZ,
        LEDC_RESOLUTION_BITS
    );


    ledcWrite(
        PIN_SALIDA_FILTRADA,
        0
    );


    // Arranque seguro
    ledcWrite(
        PIN_PWM_CONTROL,
        0
    );


    // ===================================================================
    // TIMER DEL CONTROL
    // ===================================================================

    timerControl =
        timerBegin(1000000);


    timerAttachInterrupt(
        timerControl,
        &onTimerControl
    );


    uint64_t periodo_us =
        (uint64_t)
        (
            1e6f /
            CONTROL_FS_HZ
        );


    timerAlarm(
        timerControl,
        periodo_us,
        true,
        0
    );


    // ===================================================================
    // TEMPORIZADOR IREF
    // ===================================================================

    tiempoAnteriorIref =
        millis();


    // ===================================================================
    // INFORMACION
    // ===================================================================

    Serial.println();

    Serial.println(
        "======================================"
    );

    Serial.println(
        " GENERADOR PWM + CONTROL PI"
    );

    Serial.println(
        "======================================"
    );

    Serial.println();


    Serial.print(
        "PWM GENERADOR GPIO: "
    );

    Serial.println(
        PIN_PWM_GENERADOR
    );


    Serial.print(
        "PWM CONTROL PI GPIO: "
    );

    Serial.println(
        PIN_PWM_CONTROL
    );


    Serial.print(
        "GPIO IREF DIGITAL: "
    );

    Serial.println(
        PIN_IREF_DIGITAL
    );


    Serial.println(
        "GPIO 28: 0 = 1 A | 1 = 2 A"
    );


    Serial.print(
        "Frecuencia PWM: "
    );

    Serial.print(
        FSW_HZ
    );

    Serial.println(" Hz");


    Serial.println();


    Serial.println(
        "Iref PULSADA"
    );


    Serial.print(
        "Nivel bajo: "
    );

    Serial.print(
        IREF_LOW,
        2
    );

    Serial.println(" A");


    Serial.print(
        "Nivel alto: "
    );

    Serial.print(
        IREF_HIGH,
        2
    );

    Serial.println(" A");


    Serial.print(
        "Tiempo por nivel: "
    );

    Serial.print(
        IREF_INTERVAL_MS
    );

    Serial.println(" ms");


    Serial.println(
        "Periodo completo: 200 ms"
    );


    Serial.println(
        "Frecuencia Iref: 5 Hz"
    );


    Serial.println();
}


// =======================================================================
// LOOP
// =======================================================================

void loop()
{
    // ================================================================
    // ACTUALIZAR IREF PULSADA
    // ================================================================

    actualizarIref();


    // ================================================================
    // CONTROL PI
    // ================================================================

    if (bandera_control)
    {
        bandera_control = false;


        // ------------------------------------------------------------
        // Iref ya fue actualizada automaticamente
        // ------------------------------------------------------------


        // ------------------------------------------------------------
        // MEDICION ACS712
        // ------------------------------------------------------------

        float iL_medida =
            leerCorrienteA();


        // Reproducir en GPIO33 la tension de GPIO34, filtrada
        tension_adc_filtrada +=
            FILTRO_ALPHA *
            (tension_adc_medido - tension_adc_filtrada);


        float duty_salida_filtrada =
            tension_adc_filtrada /
            TENSION_SALIDA_MAX;


        if (duty_salida_filtrada > 1.0f)
            duty_salida_filtrada = 1.0f;


        if (duty_salida_filtrada < 0.0f)
            duty_salida_filtrada = 0.0f;


        ledcWrite(
            PIN_SALIDA_FILTRADA,
            (uint32_t)(
                duty_salida_filtrada *
                LEDC_MAX_DUTY
            )
        );


        // ------------------------------------------------------------
        // ERROR
        // ------------------------------------------------------------

        float error =
            Iref +
            iL_medida;


        // ------------------------------------------------------------
        // PI
        // ------------------------------------------------------------

        duty_control =
            compensadorPI(error);


        // ------------------------------------------------------------
        // LIMITADOR DE CORRIENTE
        // ------------------------------------------------------------

        if (iL_medida > IL_MAX)
        {
            float exceso =
                iL_medida -
                IL_MAX;


            float duty_limitado =
                duty_control -
                KLIM * exceso;


            if (duty_limitado < duty_control)
            {
                duty_control =
                    duty_limitado;


                // Evitar windup
                integrador = 0.0f;
            }
        }


        // ------------------------------------------------------------
        // LIMITAR DUTY DEL CONTROL
        // ------------------------------------------------------------

        if (duty_control > 1.0f)
            duty_control = 1.0f;


        if (duty_control < 0.0f)
            duty_control = 0.0f;


        // ------------------------------------------------------------
        // PWM DE SALIDA DEL PI
        // ------------------------------------------------------------

        uint32_t cuenta_pwm =
            (uint32_t)
            (
                duty_control *
                LEDC_MAX_DUTY
            );


        ledcWrite(
            PIN_PWM_CONTROL,
            cuenta_pwm
        );


        // ------------------------------------------------------------
        // TELEMETRIA
        // ------------------------------------------------------------

        static int contador_print = 0;


        if (++contador_print >= 500)
        {
            contador_print = 0;


            Serial.print(
                "Iref="
            );

            Serial.print(
                Iref,
                3
            );


            Serial.print(
                " A   GPIO28="
            );

            Serial.print(
                digitalRead(PIN_IREF_DIGITAL)
            );


            Serial.print(
                "   Imedida="
            );

            Serial.print(
                iL_medida,
                3
            );


            Serial.print(
                " A   Error="
            );

            Serial.print(
                error,
                3
            );


            Serial.print(
                "   Duty_PI="
            );

            Serial.print(
                duty_control * 100.0f,
                2
            );


            Serial.println(" %");
        }
    }
}