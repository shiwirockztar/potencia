# Control de corriente del Boost con ESP32

Este directorio contiene `Boost_Leo.ino`. El programa lee la corriente estimada
por un ACS712 mediante el ADC del ESP32, calcula el error respecto al setpoint y
aplica un PI digital alrededor del duty nominal `D_NOMINAL = 0.4117`.

## Conexiones para Wokwi

Wokwi no simula automáticamente un ACS712 conectado a una planta Boost. Para
probar el programa de forma sencilla, se usa un potenciómetro como fuente de
voltaje equivalente a la salida del ACS:

| Elemento | Conexión ESP32 | Conexión adicional |
| --- | --- | --- |
| Potenciómetro | Terminal central a GPIO34 | Terminales laterales a 3V3 y GND |
| Salida PWM | GPIO25 | Osciloscopio o Logic Analyzer, con GND común |
| LED de estado opcional | GPIO2 o GPIO4 mediante resistencia de 220-330 ohm | Cátodo a GND |
| Alimentación | 3V3 y GND | Común para todos los elementos |

En Wokwi:

1. Cree un proyecto nuevo con una placa **ESP32 DevKit v1**.
2. Copie `Boost_Leo.ino` al archivo principal del proyecto.
3. Añada un `potentiometer`, un `logic analyzer` y, opcionalmente, dos LEDs.
4. Realice las conexiones de la tabla.
5. Inicie la simulación y abra el monitor Serial a `115200 baudios`.

No se necesita una librería externa. El código utiliza `Arduino.h` y la API
LEDC de Arduino-ESP32.

## Equivalencia del potenciómetro

El código supone:

- ACS712 alimentado a 5 V.
- Tensión de salida a corriente cero: `2.5 V`.
- Sensibilidad ACS712-05B: `0.185 V/A`.
- Divisor resistivo hacia el ADC: `0.6`.

Por tanto, el voltaje esperado en GPIO34 es:

```text
V_ADC = (2.5 V + I * 0.185 V/A) * 0.6
```

Valores útiles para probar manualmente:

| Corriente equivalente | Salida ACS | GPIO34 | ADC aproximado de 12 bits |
| ---: | ---: | ---: | ---: |
| 0 A | 2.5000 V | 1.5000 V | 1861 |
| 1.0 A | 2.6850 V | 1.6110 V | 1999 |
| 1.5 A | 2.7775 V | 1.6665 V | 2068 |
| 2.0 A | 2.8700 V | 1.7220 V | 2137 |

El potenciómetro debe ajustarse aproximadamente a esos voltajes, no a la
tensión ACS sin dividir. La lectura puede variar ligeramente por la
cuantización del ADC.

## Prueba rápida

Con la simulación iniciada:

1. Deje el setpoint inicial en `1.5 A`.
2. Coloque el potenciómetro cerca de `1.6665 V` para simular `1.5 A`.
3. Observe el monitor Serial. Deben aparecer `medicion`, `setpoint`, `error` y
	`duty`.
4. Envíe `s1.0` para pedir un escalón a `1.0 A`.
5. Cambie el potenciómetro y observe cómo cambia el error y la acción PI.
6. Envíe `i` para imprimir el estado instantáneo.
7. Envíe `d0.4117` para restaurar el duty nominal.

La frecuencia PWM esperada es `20 kHz`, con resolución de `10 bits`. En el
Logic Analyzer, GPIO25 debe mostrar una señal PWM. El duty nunca debe ser
menor que `0.0` ni mayor que `0.55`.

## Pruebas que sí pueden verificarse en Wokwi

- Compilación del sketch Arduino para ESP32, si el proyecto usa una versión
  compatible de Arduino-ESP32.
- Lectura del ADC en GPIO34.
- Conversión ADC -> voltaje -> corriente equivalente.
- Cálculo del error `setpoint - medición`.
- Ejecución periódica aproximada cada `100 us` mediante `micros()`.
- Generación de PWM en GPIO25 a `20 kHz`.
- Acción PI digital con integración trapezoidal.
- Saturación del duty entre `0.0` y `0.55`.
- Anti-windup por integración condicional.
- Visualización Serial de medición, setpoint, error y duty.

## Limitación importante de Wokwi

Con el potenciómetro se prueba la electrónica de entrada y el algoritmo, pero
no se prueba la dinámica real del Boost. El potenciómetro no cambia su valor
como consecuencia del PWM; por ello Wokwi no puede demostrar por sí solo que
la corriente física alcance el setpoint.

Para validar el lazo completo se necesita conectar el ACS712 y el convertidor
Boost reales, o crear un modelo personalizado de la planta que convierta el
duty en una corriente simulada. En la prueba física deben verificarse además
el offset real del ACS, la polaridad de la corriente, la tensión del divisor y
la compatibilidad eléctrica del GPIO34.

## Criterio de aceptación

La prueba se considera correcta cuando:

1. El sketch arranca sin errores y muestra la configuración.
2. La medición calculada coincide aproximadamente con el voltaje aplicado al
	potenciómetro según la tabla.
3. Un cambio de setpoint modifica el error y el duty.
4. El analizador observa PWM de aproximadamente `20 kHz`.
5. El duty permanece siempre dentro de `0.0...0.55`.
6. Con la planta real conectada, la corriente converge al setpoint sin que el
	duty exceda el límite superior.