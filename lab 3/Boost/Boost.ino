/* =======================================================================
   BOOST CONVERTER - ESP32

   DOS PWM INDEPENDIENTES
   =======================================================================

   PWM 1 - GENERADOR
   -----------------
   PIN_PWM_GENERADOR = GPIO 26

   Genera una onda cuadrada independiente.
   Frecuencia: 20 kHz
   Duty: configurable por Serial

   Este PWM representa la REFERENCIA para el PI.

   Ejemplo:
       0%   -> Iref = 0 A
       50%  -> Iref = 1.5 A
       100% -> Iref = 3 A


   PWM 2 - CONTROL PI
   ------------------
   PIN_PWM_CONTROL = GPIO 25

   Es la salida REAL del controlador PI.

   GPIO 25 -> UCC21520 -> MOSFET


   =======================================================================
   FLUJO
   =======================================================================

   PWM GENERADOR
        |
        | duty
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
// Señal independiente que representa la referencia
const int PIN_PWM_GENERADOR = 26;


// PWM SALIDA DEL CONTROLADOR PI
// Esta señal va al UCC21520
const int PIN_PWM_CONTROL = 25;


// ADC ACS712
const int PIN_ADC_CORRIENTE = 34;


// =======================================================================
// CONFIGURACION PWM
// =======================================================================

const uint32_t FSW_HZ = 20000;

const int LEDC_RESOLUTION_BITS = 10;

const int LEDC_MAX_DUTY =
    (1 << LEDC_RESOLUTION_BITS) - 1;


// =======================================================================
// DUTY DEL GENERADOR
// =======================================================================

// Duty inicial del generador
// 0.50 = 50 %
float duty_generador = 0.50f;


// =======================================================================
// CONVERSION DEL GENERADOR A IREF
// =======================================================================

// Corriente máxima representada por el PWM generador
//
// 0%   -> 0 A
// 50%  -> 1.5 A
// 100% -> 3 A

const float IREF_MAX = 3.0f;


// =======================================================================
// ACS712
// =======================================================================

// Divisor resistivo
//
// ACS712 -> divisor -> ADC ESP32

const float DIVISOR_RATIO = 0.6f;


// Sensibilidad medida
const float ACS712_SENS = 0.1891f;


// Offset medido
float acs712_offset_V = 2.3853f;


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


const float KP = 0.4912f;

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
    float v_adc_mV =
        analogReadMilliVolts(PIN_ADC_CORRIENTE);


    float v_adc =
        v_adc_mV / 1000.0f;


    // Recuperar tensión original del ACS712
    float v_acs =
        v_adc / DIVISOR_RATIO;


    // Convertir tensión a corriente
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
    // Integración trapezoidal
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
// COMANDOS SERIAL
// =======================================================================

void procesarSerial()
{
    if (!Serial.available())
        return;


    float nuevoDuty =
        Serial.parseFloat();


    // ---------------------------------------------------------------
    // DUTY DEL GENERADOR
    // ---------------------------------------------------------------

    if (
        nuevoDuty >= 0.0f &&
        nuevoDuty <= 100.0f
    )
    {
        duty_generador =
            nuevoDuty / 100.0f;


        actualizarPWMGenerador();


        Serial.println();

        Serial.print(
            "PWM GENERADOR = "
        );

        Serial.print(
            nuevoDuty,
            2
        );

        Serial.println(" %");


        // Calcular Iref equivalente
        float iref =
            duty_generador *
            IREF_MAX;


        Serial.print(
            "Iref equivalente = "
        );

        Serial.print(
            iref,
            3
        );

        Serial.println(" A");

        Serial.println();
    }


    // Limpiar buffer
    while (Serial.available())
        Serial.read();
}


// =======================================================================
// SETUP
// =======================================================================

void setup()
{
    Serial.begin(115200);

    delay(500);


    // ===================================================================
    // ADC
    // ===================================================================

    analogReadResolution(12);


    analogSetPinAttenuation(
        PIN_ADC_CORRIENTE,
        ADC_11db
    );


    // ===================================================================
    // PWM GENERADOR
    // ===================================================================

    ledcAttach(
        PIN_PWM_GENERADOR,
        FSW_HZ,
        LEDC_RESOLUTION_BITS
    );


    // Duty inicial = 50 %
    actualizarPWMGenerador();


    // ===================================================================
    // PWM CONTROL PI
    // ===================================================================

    ledcAttach(
        PIN_PWM_CONTROL,
        FSW_HZ,
        LEDC_RESOLUTION_BITS
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
        "Frecuencia PWM: "
    );

    Serial.print(
        FSW_HZ
    );

    Serial.println(" Hz");


    Serial.println();

    Serial.println(
        "Escribe un valor de 0 a 100"
    );

    Serial.println(
        "para cambiar el duty del GENERADOR."
    );

    Serial.println();


    Serial.println(
        "Ejemplo:"
    );

    Serial.println(
        "50 = 50% = Iref 1.5 A"
    );

    Serial.println(
        "25 = 25% = Iref 0.75 A"
    );

    Serial.println(
        "75 = 75% = Iref 2.25 A"
    );

    Serial.println();
}


// =======================================================================
// LOOP
// =======================================================================

void loop()
{
    // ================================================================
    // SERIAL
    // ================================================================

    procesarSerial();


    // ================================================================
    // CONTROL PI
    // ================================================================

    if (bandera_control)
    {
        bandera_control = false;


        // ------------------------------------------------------------
        // REFERENCIA GENERADA POR EL PWM
        // ------------------------------------------------------------

        float Iref =
            duty_generador *
            IREF_MAX;


        // ------------------------------------------------------------
        // MEDICION ACS712
        // ------------------------------------------------------------

        float iL_medida =
            leerCorrienteA();


        // ------------------------------------------------------------
        // ERROR
        // ------------------------------------------------------------

        float error =
            Iref -
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
                " A   Imedida="
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