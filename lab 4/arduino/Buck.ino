/*
   Control de tension del buck con realimentacion aislada IL300.

   La funcion de transferencia del esquema buck_control.asc es:

       (0.269881*s + 433.32) / (s + 1e-9)

   Por tanto, el controlador implementado es un PI con:
       Kp = 0.269881
       Ki = 433.32

   El IL300 debe entregar al ADC la senal Vsense del receptor. En el
   esquema, R1=18 kOhm y R2=2 kOhm forman un divisor 10:1, por lo que
   Vout=10 V equivale aproximadamente a Vsense=1 V.
*/

#include <Arduino.h>

// Entrada analogica conectada a la salida del amplificador receptor del IL300.
const int PIN_ADC_IL300 = 34;

// Entrada PWM del driver UCC21520.
const int PIN_PWM_CONTROL = 25;

// PWM generador de referencia para pruebas.
const int PIN_PWM_GENERADOR = 26;

// Salida PWM de la realimentacion filtrada.
const int PIN_SALIDA_FILTRADA = 33;

// Indicador del nivel alto o bajo de la referencia.
const int PIN_REFERENCIA_DIGITAL = 27;

const uint32_t FSW_HZ = 20000;
const float CONTROL_FS_HZ = 10000.0f;
const float TS = 1.0f / CONTROL_FS_HZ;
const int LEDC_RESOLUTION_BITS = 10;
const uint32_t LEDC_MAX_DUTY =
    (1U << LEDC_RESOLUTION_BITS) - 1U;

// Referencia en el nodo Vsense. Con el divisor 18 kOhm/2 kOhm:
// 1.0 V en Vsense corresponde a aproximadamente 10 V en Vout.
const float VOLTAGE_REFERENCE_SENSE_V = 1.0f;

// El duty introducido por serie representa 0..1.5 V en Vsense.
const float VOLTAGE_REFERENCE_PWM_MAX_V = 1.5f;

// Si existe otro divisor entre el receptor IL300 y el ADC, ajustar este
// valor: V_sense = V_adc / IL300_ADC_SCALE.
const float IL300_ADC_SCALE = 1.0f;

const int NUM_MUESTRAS_IL300 = 8;
const float FILTRO_IL300_ALPHA = 0.05f;

// Coeficientes obtenidos directamente de buck_control.asc.
const float KP = 0.269881f;
const float KI = 433.32f;

float feedback_filtrado_V = 0.0f;
float voltage_reference_sense_V = VOLTAGE_REFERENCE_SENSE_V;
float integrador = 0.0f;
float error_anterior = 0.0f;
float duty_control = 0.0f;
float duty_generador_porcentaje =
    VOLTAGE_REFERENCE_SENSE_V / VOLTAGE_REFERENCE_PWM_MAX_V * 100.0f;

hw_timer_t *timerControl = NULL;
volatile bool bandera_control = false;

void IRAM_ATTR onTimerControl()
{
    bandera_control = true;
}

float leerFeedbackIL300V()
{
    uint32_t suma_adc_mV = 0;

    for (int muestra = 0; muestra < NUM_MUESTRAS_IL300; muestra++)
        suma_adc_mV += analogReadMilliVolts(PIN_ADC_IL300);

    const float adc_V =
        (suma_adc_mV / (float)NUM_MUESTRAS_IL300) / 1000.0f;

    return adc_V / IL300_ADC_SCALE;
}

float compensadorPI(float error)
{
    // Integracion trapezoidal, equivalente a la discretizacion del PI
    // de la funcion Laplace del esquema.
    const float delta_integrador =
        KI * TS * 0.5f * (error + error_anterior);

    const float salida_sin_sat =
        KP * error + integrador + delta_integrador;

    const bool saturado_alto = salida_sin_sat > 1.0f;
    const bool saturado_bajo = salida_sin_sat < 0.0f;

    if (!((saturado_alto && error > 0.0f) ||
          (saturado_bajo && error < 0.0f)))
    {
        integrador += delta_integrador;
    }

    error_anterior = error;

    float salida = KP * error + integrador;

    if (salida > 1.0f)
        salida = 1.0f;
    if (salida < 0.0f)
        salida = 0.0f;

    return salida;
}

void escribirPWM(float duty)
{
    if (duty > 1.0f)
        duty = 1.0f;
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

void leerDutyPorSerie()
{
    if (!Serial.available())
        return;

    String entrada = Serial.readStringUntil('\n');
    entrada.trim();

    if (entrada.length() == 0)
        return;

    const float duty_recibido = entrada.toFloat();

    if (duty_recibido < 0.0f || duty_recibido > 100.0f)
    {
        Serial.println("Error: introduce un duty entre 0 y 100.");
        return;
    }

    duty_generador_porcentaje = duty_recibido;
    voltage_reference_sense_V =
        duty_generador_porcentaje / 100.0f *
        VOLTAGE_REFERENCE_PWM_MAX_V;

    digitalWrite(
        PIN_REFERENCIA_DIGITAL,
        duty_generador_porcentaje >= 50.0f ? HIGH : LOW
    );

    actualizarPWMGenerador();

    Serial.print("Duty PWM GPIO26 actualizado: ");
    Serial.print(duty_generador_porcentaje, 2);
    Serial.println(" %");
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    analogReadResolution(12);
    analogSetPinAttenuation(PIN_ADC_IL300, ADC_11db);

    pinMode(PIN_ADC_IL300, INPUT);
    pinMode(PIN_REFERENCIA_DIGITAL, OUTPUT);
    digitalWrite(PIN_REFERENCIA_DIGITAL, duty_generador_porcentaje >= 50.0f);

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

    Serial.println("Control de tension buck con IL300 + generador PWM");
    Serial.print("PWM generador: GPIO ");
    Serial.println(PIN_PWM_GENERADOR);
    Serial.print("Entrada ADC IL300: GPIO ");
    Serial.println(PIN_ADC_IL300);
    Serial.print("Referencia Vsense: ");
    Serial.print(VOLTAGE_REFERENCE_SENSE_V, 3);
    Serial.println(" V");
    Serial.print("PWM de control: GPIO ");
    Serial.println(PIN_PWM_CONTROL);
    Serial.print("PWM realimentacion filtrada: GPIO ");
    Serial.println(PIN_SALIDA_FILTRADA);
    Serial.print("Indicador referencia: GPIO ");
    Serial.println(PIN_REFERENCIA_DIGITAL);
    Serial.println("Introduce el duty del PWM GPIO26 (0 a 100) y pulsa Enter:");
}

void loop()
{
    leerDutyPorSerie();

    if (!bandera_control)
        return;

    bandera_control = false;

    const float feedback_V = leerFeedbackIL300V();

    feedback_filtrado_V +=
        FILTRO_IL300_ALPHA *
        (feedback_V - feedback_filtrado_V);

    // Error positivo: falta tension, por lo que el PI aumenta el duty.
    const float error =
        voltage_reference_sense_V - feedback_filtrado_V;

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

    static uint16_t contador_print = 0;
    if (++contador_print >= 500)
    {
        contador_print = 0;

        Serial.print("Vref=");
        Serial.print(voltage_reference_sense_V, 3);
        Serial.print(" V   Vsense=");
        Serial.print(feedback_filtrado_V, 3);
        Serial.print(" V   Error=");
        Serial.print(error, 3);
        Serial.print(" V   Duty=");
        Serial.print(duty_control * 100.0f, 2);
        Serial.println(" %");
    }
}
