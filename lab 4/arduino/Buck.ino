/*
   Control de tension del buck con realimentacion aislada IL300.

   La etapa de potencia actual usa L=4 mH y C=22 uF. Los coeficientes
   anteriores procedian de otro modelo y producian una integral demasiado
   agresiva para esta etapa.

   La sintonizacion actual busca un tiempo de establecimiento aproximado de
   30 ms. El tiempo integral se fija en 15 ms para que el PI corrija el error
   estacionario sin intentar seguir el ruido del ADC.

   El IL300 debe entregar al ADC la senal Vsense del receptor. En el
   esquema, R1=18 kOhm y R2=2 kOhm forman un divisor 10:1, por lo que
   Vout=10 V equivale aproximadamente a Vsense=1 V.
*/

#include <Arduino.h>

// ADC de realimentacion de tension del buck.
const int PIN_ADC_FEEDBACK = 34;

// Entrada PWM del driver UCC21520.
const int PIN_PWM_CONTROL = 25;

// PWM generador de referencia para pruebas.
const int PIN_PWM_GENERADOR = 26;

// Salida PWM de la realimentacion filtrada.
const int PIN_SALIDA_FILTRADA = 33;

// Indicador de referencia. Mismo GPIO que el Boost.
const int PIN_IREF_DIGITAL = 27;

const uint32_t FSW_HZ = 20000;
const float CONTROL_FS_HZ = 10000.0f;
const float TS = 1.0f / CONTROL_FS_HZ;
const float VIN_BUCK_V = 17.0f;
const int LEDC_RESOLUTION_BITS = 10;
const uint32_t LEDC_MAX_DUTY =
    (1U << LEDC_RESOLUTION_BITS) - 1U;
const float DUTY_MAX = 0.85f;

// Referencias de salida del buck. Con el divisor 18 kOhm/2 kOhm,
// Vout/10 corresponde aproximadamente a Vsense.
const float VOUT_REFERENCE_LOW_V = 7.0f;
const float VOUT_REFERENCE_HIGH_V = 10.0f;
// Debe ser mayor que el tiempo de establecimiento para poder observar cada
// escalon de referencia durante las pruebas.
const uint32_t REFERENCE_INTERVAL_MS = 50;

// El duty del generador representa 0..1.5 V en Vsense.
const float VOLTAGE_REFERENCE_PWM_MAX_V = 1.5f;

const int NUM_MUESTRAS_IL300 = 1;
const float FILTRO_IL300_ALPHA = 1.0f;

// Calibracion medida en el ADC: 7 V -> 0.1406 V y 12 V -> 0.7500 V.
const float IL300_ADC_AT_7V = 0.1406f;
const float IL300_ADC_AT_12V = 0.7500f;
const float IL300_ADC_SLOPE =
    (IL300_ADC_AT_12V - IL300_ADC_AT_7V) / 5.0f;

// Parametros de la etapa de potencia actual.
const float SETTLING_TIME_TARGET_S = 0.010f;
const float INTEGRAL_TIME_S = 0.0007f;

// PI de respuesta rapida para L=4 mH, C=22 uF y muestreo a 10 kHz.
// Ti = Kp / Ki = 0.7 ms.
const float KP = 2.50f;
const float KI = KP / INTEGRAL_TIME_S;

float feedback_filtrado_V = 0.0f;
float voltage_reference_adc_V = IL300_ADC_AT_7V;
float integrador = VOUT_REFERENCE_LOW_V / VIN_BUCK_V;
float duty_control = 0.0f;
float duty_generador_porcentaje =
    voltage_reference_adc_V / VOLTAGE_REFERENCE_PWM_MAX_V * 100.0f;
bool referencia_alta = false;
uint32_t tiempo_referencia_anterior = 0;

hw_timer_t *timerControl = NULL;
volatile bool bandera_control = false;

void IRAM_ATTR onTimerControl()
{
    bandera_control = true;
}

float leerFeedbackADC_V()
{
    uint32_t suma_adc_mV = 0;

    for (int muestra = 0; muestra < NUM_MUESTRAS_IL300; muestra++)
        suma_adc_mV += analogReadMilliVolts(PIN_ADC_FEEDBACK);

    const float adc_V =
        (suma_adc_mV / (float)NUM_MUESTRAS_IL300) / 1000.0f;

    return adc_V;
}

float compensadorPI(float error)
{
    const float salida_proporcional = KP * error;
    const float salida_sin_sat = salida_proporcional + integrador;

    const bool saturado_alto = salida_sin_sat >= DUTY_MAX;
    const bool saturado_bajo = salida_sin_sat <= 0.0f;

    if (!((saturado_alto && error > 0.0f) ||
          (saturado_bajo && error < 0.0f)))
    {
        integrador += KI * TS * error;
    }

    float salida = salida_proporcional + integrador;

    if (salida > DUTY_MAX)
        salida = DUTY_MAX;
    if (salida < 0.0f)
        salida = 0.0f;

    return salida;
}

void escribirPWM(float duty)
{
    if (duty > DUTY_MAX)
        duty = DUTY_MAX;
    if (duty < 0.0f)
        duty = 0.0f;

    ledcWrite(
        PIN_PWM_CONTROL,
        (uint32_t)(duty * LEDC_MAX_DUTY)
    );
}

void actualizarPWMGenerador()
{
    const float duty_generador =
        duty_generador_porcentaje / 100.0f;

    ledcWrite(
        PIN_PWM_GENERADOR,
        (uint32_t)(duty_generador * LEDC_MAX_DUTY)
    );
}

void actualizarReferenciaAutomatica()
{
    const uint32_t tiempo_actual = millis();

    if (tiempo_actual - tiempo_referencia_anterior < REFERENCE_INTERVAL_MS)
        return;

    tiempo_referencia_anterior = tiempo_actual;
    referencia_alta = !referencia_alta;

    const float referencia_vout = referencia_alta
        ? VOUT_REFERENCE_HIGH_V
        : VOUT_REFERENCE_LOW_V;

    voltage_reference_adc_V =
        IL300_ADC_AT_7V + IL300_ADC_SLOPE * (referencia_vout - 7.0f);
    integrador = referencia_vout / VIN_BUCK_V;
    duty_generador_porcentaje =
        voltage_reference_adc_V / VOLTAGE_REFERENCE_PWM_MAX_V * 100.0f;

    digitalWrite(PIN_IREF_DIGITAL, referencia_alta ? HIGH : LOW);
    actualizarPWMGenerador();
}

void setup()
{
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_ADC_FEEDBACK, ADC_11db);

    pinMode(PIN_ADC_FEEDBACK, INPUT);
    pinMode(PIN_IREF_DIGITAL, OUTPUT);
    digitalWrite(PIN_IREF_DIGITAL, duty_generador_porcentaje >= 50.0f);

    ledcAttach(
        PIN_PWM_GENERADOR,
        FSW_HZ,
        LEDC_RESOLUTION_BITS
    );
    actualizarPWMGenerador();

    ledcAttach(
        PIN_PWM_CONTROL,
        FSW_HZ,
        LEDC_RESOLUTION_BITS
    );
    escribirPWM(0.0f);

    ledcAttach(
        PIN_SALIDA_FILTRADA,
        FSW_HZ,
        LEDC_RESOLUTION_BITS
    );
    ledcWrite(PIN_SALIDA_FILTRADA, 0);

    timerControl = timerBegin(1000000);
    timerAttachInterrupt(timerControl, &onTimerControl);

    const uint64_t periodo_us =
        (uint64_t)(1e6f / CONTROL_FS_HZ);
    timerAlarm(timerControl, periodo_us, true, 0);
}

void loop()
{
    actualizarReferenciaAutomatica();

    if (!bandera_control)
        return;

    bandera_control = false;

    const float feedback_V = leerFeedbackADC_V();

    feedback_filtrado_V +=
        FILTRO_IL300_ALPHA *
        (feedback_V - feedback_filtrado_V);

    // Error positivo: falta tension, por lo que el PI aumenta el duty.
    const float error =
        voltage_reference_adc_V - feedback_filtrado_V;

    duty_control = compensadorPI(error);
    escribirPWM(duty_control);

    float duty_feedback =
        feedback_filtrado_V / VOLTAGE_REFERENCE_PWM_MAX_V;
    if (duty_feedback > 1.0f)
        duty_feedback = 1.0f;
    if (duty_feedback < 0.0f)
        duty_feedback = 0.0f;

    ledcWrite(
        PIN_SALIDA_FILTRADA,
        (uint32_t)(duty_feedback * LEDC_MAX_DUTY)
    );
}
