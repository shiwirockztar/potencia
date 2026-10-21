#include <Arduino.h>

// Conectar estas salidas a INA e INB del UCC21520.
constexpr uint8_t PIN_SPWM_POSITIVA = 25;
constexpr uint8_t PIN_SPWM_NEGATIVA = 26;

constexpr uint32_t PWM_FREQUENCY_HZ = 10000;
constexpr uint8_t PWM_RESOLUTION_BITS = 11;
constexpr uint16_t PWM_PERIOD = 1600;
constexpr uint16_t LEDC_MAX_DUTY =
    (1U << PWM_RESOLUTION_BITS) - 1U;
constexpr uint16_t TABLE_SIZE = 200;

// lookUp1: semiciclo positivo primero y ceros despues.
const uint16_t lookUp1[TABLE_SIZE] = {
    50, 100, 151, 201, 250, 300, 349, 398, 446, 494,
    542, 589, 635, 681, 726, 771, 814, 857, 899, 940,
    981, 1020, 1058, 1095, 1131, 1166, 1200, 1233, 1264, 1294,
    1323, 1351, 1377, 1402, 1426, 1448, 1468, 1488, 1505, 1522,
    1536, 1550, 1561, 1572, 1580, 1587, 1593, 1597, 1599, 1600,
    1599, 1597, 1593, 1587, 1580, 1572, 1561, 1550, 1536, 1522,
    1505, 1488, 1468, 1448, 1426, 1402, 1377, 1351, 1323, 1294,
    1264, 1233, 1200, 1166, 1131, 1095, 1058, 1020, 981, 940,
    899, 857, 814, 771, 726, 681, 635, 589, 542, 494,
    446, 398, 349, 300, 250, 201, 151, 100, 50, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// lookUp2: ceros primero y semiciclo negativo despues.
const uint16_t lookUp2[TABLE_SIZE] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    50, 100, 151, 201, 250, 300, 349, 398, 446, 494,
    542, 589, 635, 681, 726, 771, 814, 857, 899, 940,
    981, 1020, 1058, 1095, 1131, 1166, 1200, 1233, 1264, 1294,
    1323, 1351, 1377, 1402, 1426, 1448, 1468, 1488, 1505, 1522,
    1536, 1550, 1561, 1572, 1580, 1587, 1593, 1597, 1599, 1600,
    1599, 1597, 1593, 1587, 1580, 1572, 1561, 1550, 1536, 1522,
    1505, 1488, 1468, 1448, 1426, 1402, 1377, 1351, 1323, 1294,
    1264, 1233, 1200, 1166, 1131, 1095, 1058, 1020, 981, 940,
    899, 857, 814, 771, 726, 681, 635, 589, 542, 494,
    446, 398, 349, 300, 250, 201, 151, 100, 50, 0
};

hw_timer_t *spwmTimer = nullptr;
volatile uint16_t phase = 0;

uint16_t IRAM_ATTR dutyFromTable(uint16_t tableValue)
{
    return (uint32_t)tableValue * LEDC_MAX_DUTY / PWM_PERIOD;
}

void IRAM_ATTR onSpwmTimer()
{
    ledcWrite(
        PIN_SPWM_POSITIVA,
        dutyFromTable(lookUp1[phase])
    );
    ledcWrite(
        PIN_SPWM_NEGATIVA,
        dutyFromTable(lookUp2[phase])
    );

    phase++;
    if (phase >= TABLE_SIZE)
        phase = 0;
}

void setup()
{
    ledcAttach(
        PIN_SPWM_POSITIVA,
        PWM_FREQUENCY_HZ,
        PWM_RESOLUTION_BITS
    );
    ledcAttach(
        PIN_SPWM_NEGATIVA,
        PWM_FREQUENCY_HZ,
        PWM_RESOLUTION_BITS
    );

    ledcWrite(PIN_SPWM_POSITIVA, 0);
    ledcWrite(PIN_SPWM_NEGATIVA, 0);

    // 10 kHz de actualizacion / 200 pasos = 50 Hz fundamental.
    spwmTimer = timerBegin(1000000);
    timerAttachInterrupt(spwmTimer, &onSpwmTimer);
    timerAlarm(
        spwmTimer,
        1000000UL / PWM_FREQUENCY_HZ,
        true,
        0
    );
}

void loop()
{
}