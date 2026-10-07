# BuckV2: control PI del convertidor buck

Este programa controla el duty de un convertidor buck con un ESP32. Lee la
realimentacion de tension por el ADC, calcula un controlador PI y genera un
PWM de 20 kHz para el interruptor o driver del buck.

## Funcionamiento

En cada vuelta del `loop()` se hace lo siguiente:

1. Se lee la tension del ADC.
2. Cada 20 ms se cambia la referencia entre `setpointHigh` y `setpointLow`.
3. El PI calcula el duty a partir del error:

   ```text
   error = referencia - tension_medida
   integral = integral + Ki * error * dt
   duty = Kp * error + integral
   ```

4. El duty se limita entre 5 % y 95 % para evitar los extremos del PWM.
5. Cada 50 ms se envian los datos al puerto serie para observarlos con el
   Serial Plotter.

El pin `PIN_SYNC` cambia junto con la referencia: permanece en `HIGH` para la
referencia alta y en `LOW` para la baja.

## Pines y parametros

| Elemento | GPIO | Funcion |
| --- | ---: | --- |
| `PIN_PWM` | 26 | PWM de control del buck |
| `PIN_ADC` | 27 | Entrada de tension medida |
| `PIN_SYNC` | 25 | Indicador digital de referencia |

Parametros iniciales:

- Frecuencia PWM: `20 kHz`
- Resolucion PWM: `10 bits` (valor maximo `1023`)
- Referencia alta: `12 V`
- Referencia baja: `7 V`
- `Kp`: `0.01`
- `Ki`: `35.0`

### Conversion del ADC

El codigo usa `analogReadMilliVolts()` y multiplica el resultado por `0.0108`.
Esto supone una ganancia total de 10.8 entre la tension real del buck y la
tension que llega al ADC. Si se cambia el divisor o la calibracion, hay que
cambiar ese factor en `updateControl()`.

## Conexion basica

- Conectar la salida PWM del ESP32 al circuito de disparo del buck.
- Conectar la senal de realimentacion al `PIN_ADC` respetando el rango maximo
  permitido por el ESP32.
- Compartir la referencia de masa entre el ESP32, el sensor y el driver cuando
  el aislamiento del montaje lo permita.
- Usar `PIN_SYNC` como senal de sincronismo o indicador de la referencia.

Antes de alimentar el circuito de potencia, probar con una fuente limitada en
corriente y verificar con un osciloscopio que el PWM no supera el duty esperado.

## Comandos por puerto serie

Configurar el monitor serie a `115200 baudios`. Cada comando debe terminar con
Enter:

| Comando | Accion | Ejemplo |
| --- | --- | --- |
| `kp=valor` | Cambia la ganancia proporcional | `kp=0.02` |
| `ki=valor` | Cambia la ganancia integral | `ki=30` |
| `sph=valor` | Cambia la referencia alta en voltios | `sph=12` |
| `spl=valor` | Cambia la referencia baja en voltios | `spl=7` |
| `info` | Muestra parametros y valores actuales | `info` |

Los cambios se mantienen solo mientras el ESP32 esta encendido, porque no se
guardan en memoria no volatil.

## Serial Plotter

El programa envia tres columnas separadas por comas cada 50 ms:

```text
Setpoint,Voltage,Duty_x10
```

El duty se multiplica por 10 unicamente para que pueda verse junto a las
referencias y la tension en el Serial Plotter. El duty real sigue estando entre
`0.05` y `0.95`.

## Ajuste del PI

1. Empezar con un `Ki` pequeno o igual a cero.
2. Aumentar `Kp` hasta obtener una respuesta rapida sin oscilaciones excesivas.
3. Aumentar `Ki` poco a poco para eliminar el error permanente.
4. Vigilar la tension, la corriente y la temperatura del convertidor.

El limite de la integral evita que el controlador siga creciendo cuando el duty
ya esta saturado, pero no sustituye a las protecciones de hardware.

## Carga del programa

Abrir `BuckV2.ino` en Arduino IDE o PlatformIO, seleccionar una placa ESP32,
comprobar el puerto serie y cargar el programa. Este sketch usa la API LEDC
actual de Arduino-ESP32 (`ledcAttach` y `ledcWrite`), por lo que conviene usar
una version reciente del core de ESP32.
