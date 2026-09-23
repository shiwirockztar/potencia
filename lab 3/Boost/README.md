# Controlador Boost con ESP32

Este proyecto controla un convertidor Boost mediante dos señales PWM:

- Un PWM generador en GPIO26, usado para definir la referencia de corriente.
- Un PWM controlado por PI en GPIO25, conectado al controlador de puerta del MOSFET.

La corriente se mide con un sensor ACS712. La salida del sensor pasa por un divisor resistivo antes de llegar al ADC del ESP32, concretamente al GPIO34.

## Archivo principal

El programa se encuentra en `Boost.ino` y está pensado para un ESP32 usando el core de Arduino.

## Pines

| Función | GPIO |
| --- | ---: |
| PWM generador de referencia | 26 |
| PWM de control PI | 25 |
| Entrada analógica de corriente | 34 |

GPIO34 es solamente una entrada ADC. No puede utilizarse como salida digital.

## Funcionamiento general

1. El PWM de GPIO26 se configura con un duty inicial del 50 %.
2. El duty del generador se transforma en una corriente de referencia:

   ```text
   Iref = duty_generador * 3.0 A
   ```

   Por tanto:

   - 0 % equivale a 0 A.
   - 50 % equivale a 1.5 A.
   - 100 % equivale a 3 A.

3. Un temporizador ejecuta el control cada 100 us, es decir, a 10 kHz.
4. En cada ciclo se lee la corriente del ACS712 por GPIO34.
5. El controlador PI calcula el duty de GPIO25 para intentar que la corriente medida siga a `Iref`.
6. Si la corriente supera `IL_MAX = 2.0 A`, se reduce adicionalmente el duty de control.
7. La telemetría se envía por Serial a 115200 baudios aproximadamente 20 veces por segundo.

## Cómo se mide la corriente

La función `leerCorrienteA()` realiza estos pasos:

### 1. Lectura del ADC

El ADC del ESP32 está configurado con resolución de 12 bits y atenuación de 11 dB:

```cpp
analogReadResolution(12);
analogSetPinAttenuation(PIN_ADC_CORRIENTE, ADC_11db);
```

La lectura se obtiene en milivoltios mediante:

```cpp
float v_adc_mV = analogReadMilliVolts(PIN_ADC_CORRIENTE);
float v_adc = v_adc_mV / 1000.0f;
```

`analogReadMilliVolts()` devuelve la tensión aproximada presente en GPIO34.

Para reducir el ruido, el programa toma 8 lecturas consecutivas y utiliza su promedio. El número de muestras se puede cambiar en `NUM_MUESTRAS_CORRIENTE`.

Más muestras normalmente producen una lectura más estable, pero también aumentan el tiempo empleado en cada ciclo de control. Por eso conviene aumentar este valor gradualmente y comprobar que el control siga respondiendo correctamente.

Además, al arrancar se toman 128 muestras con los PWM todavía desactivados para calibrar automáticamente el punto de cero. Durante esta calibración no debe circular corriente por el ACS712. El valor calculado se muestra por el monitor Serial como `Offset ACS712 calibrado` y reemplaza el valor inicial de `acs712_offset_V`.

### 2. Recuperación de la tensión del ACS712

El código usa:

El divisor está formado por una resistencia de `10 kOhm` entre la salida del ACS712 y el nodo del ADC, y una resistencia de `20 kOhm` entre el nodo del ADC y tierra:

```text
ACS712 Vout --- 10 kOhm --- GPIO34 --- 20 kOhm --- GND
```

Por tanto, el divisor entrega al ADC:

```text
Vadc = Vacs * 20 / (10 + 20)
Vadc = Vacs * 0.6667
```

En el código, esta relación se expresa como `DIVISOR_RATIO = 20 / (10 + 20)`. Para recuperar la tensión original del sensor:

```text
Vacs = Vadc / 0.6667
```

### 3. Conversión de tensión a corriente

Los parámetros utilizados son:

```cpp
const float ACS712_SENS = 0.1891f;
float acs712_offset_V = 2.3853f;
```

La fórmula completa es:

```text
I medida = (Vacs - 2.3853 V) / 0.1891 V/A
```

En una sola expresión:

```text
I medida = ((Vadc / 0.6667) - 2.3853) / 0.1891
```

El resultado se expresa en amperios y se almacena en `iL_medida`.

### Ejemplo

Si el ADC mide aproximadamente `1.5902 V` después del divisor:

```text
Vacs = 1.5902 / 0.6667 = 2.3853 V
I medida = (2.3853 - 2.3853) / 0.1891 = 0 A
```

Si el ACS712 entrega `2.5744 V` antes del divisor:

```text
I medida = (2.5744 - 2.3853) / 0.1891 = 1 A aproximadamente
```

## Conexión de la medida

La conexión conceptual es:

```text
ACS712 Vout -> divisor resistivo -> GPIO34 (ADC ESP32)
ACS712 GND  ---------------------> GND ESP32
ESP32 GND   ---------------------> GND del sistema
```

El divisor debe garantizar que la tensión máxima que llega a GPIO34 sea segura para el ESP32. No se debe conectar directamente una señal que pueda superar el rango permitido por el ADC.

## Calibración

El valor `acs712_offset_V = 2.3853` representa la tensión de salida del ACS712 cuando la corriente es cero, según la calibración actual.

Para recalibrarlo:

1. Dejar el convertidor sin corriente por el ACS712.
2. Medir la tensión real de salida del ACS712, antes del divisor.
3. Sustituir `2.3853f` por ese valor en voltios. Ese valor debe ser la salida del ACS712 antes del divisor, porque el programa recupera primero `Vacs`.
4. Comprobar la lectura mostrada por Serial.

La sensibilidad `0.1891 V/A` también debe corresponder al sensor utilizado y a la calibración experimental. Si se cambia el ACS712 o el circuito analógico, hay que recalibrar ambos parámetros.

## Monitor Serial

Abrir el monitor Serial a `115200` baudios. La telemetría tiene este formato:

```text
Iref=1.500 A   Imedida=1.234 A   Error=0.266   Duty_PI=42.50 %
```

- `Iref`: corriente solicitada por el PWM generador.
- `Imedida`: corriente calculada a partir de GPIO34.
- `Error`: diferencia entre la referencia y la corriente medida.
- `Duty_PI`: duty aplicado al PWM de control en GPIO25.

Para cambiar la referencia, enviar por Serial un valor entre `0` y `100`, que representa el duty del PWM generador en porcentaje. Por ejemplo, enviar `50` establece una referencia aproximada de `1.5 A`.

## Parámetros principales

| Parámetro | Valor | Descripción |
| --- | ---: | --- |
| `FSW_HZ` | 20000 Hz | Frecuencia de ambos PWM |
| `CONTROL_FS_HZ` | 10000 Hz | Frecuencia del control PI |
| `IREF_MAX` | 3.0 A | Corriente máxima de referencia |
| `IL_MAX` | 2.0 A | Límite de corriente |
| `DIVISOR_RATIO` | 0.6667 | Relación del divisor de 10 kOhm y 20 kOhm |
| `ACS712_SENS` | 0.1891 V/A | Sensibilidad calibrada |
| `acs712_offset_V` | 2.3853 V | Offset a corriente cero |
| `KP` | 0.4912 | Ganancia proporcional |
| `KI` | 750.2 | Ganancia integral |

## Advertencias

- Verificar siempre el divisor resistivo antes de conectar el ACS712 al ESP32.
- Compartir una referencia de tierra adecuada entre el sensor y el ESP32.
- La lectura depende de la calibración del offset y de la sensibilidad del sensor.
- El valor mostrado puede tener ruido; para mejorar la estabilidad de la medida puede ser necesario promediar varias muestras, lo que cambiaría el código de lectura actual.
