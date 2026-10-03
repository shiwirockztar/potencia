# Control de corriente del buck en LTspice

## Objetivo

El esquema `buckboost.asc` tiene actualmente una fuente `V2` conectada a
`INA` del `UCC21520_TRANS`:

```text
PULSE(0 5 0 1n 1n 29u 50u)
```

Esta señal conmuta a 20 kHz con un duty fijo del 58 %. Por tanto, la
corriente del buck cambia cuando cambia la entrada, la carga o el punto de
operacion. Para regularla no se debe aplicar la salida analogica del PID
directamente a `INA`: `INA` es una entrada logica del driver y necesita una
señal PWM de aproximadamente 0 V/5 V.

La estructura correcta es:

```text
Iref -> error -> PI de corriente -> duty -> comparador con SAW -> INA
  ^                                                       |
  |                                                       v
   +------------- V(ACS_OUT), corriente del buck <- potencia
```

El PI calcula el duty. El comparador convierte ese duty en el PWM que entra
al `UCC21520_TRANS`.

## Señal que debe entrar en INA

Para una frecuencia de conmutacion de 20 kHz:

```text
Tsw = 1 / 20 kHz = 50 us
```

La señal que debe llegar a `INA` es:

```text
V(INA) = 5 V, si V(duty) > V(SAW)
V(INA) = 0 V, en caso contrario
```

`SAW` debe ser una rampa de 0 a 1 V en cada periodo de 50 us. El nodo
`duty` debe estar limitado entre 0 y 1. Asi, un `duty = 0.58` produce el
mismo tiempo de conduccion que la fuente actual de 29 us, pero el duty puede
corregirse automaticamente.

## Cambio minimo en `buckboost.asc`

1. Retirar o desconectar `V2` de la red `PULSE` que llega a `INA`.
2. No conservar `V4` tal como esta. Aunque el nodo se llama `SAW`,
   `V4` esta definido como `PULSE(0 1 0 50u 1f 1p 50u)` y no genera una
   rampa: solo permanece en 1 V durante aproximadamente 1 ps. Desconectar
   `V4` y generar una rampa real en el mismo nodo `SAW`:

```spice
BSAW SAW 0 V={mod(time,50u)/50u}
```

   Esta rampa va de 0 a 1 V cada 50 us, por lo que corresponde a 20 kHz.
   Conservar el etiquetado `SAW` y la frecuencia de 20 kHz.
3. Usar el `ACS712` para medir la corriente de la rama del buck. El sensor
   debe quedar en serie con `L1`, de modo que la corriente entre por `I+` y
   salga por `I-`. En el esquema, la rama del buck es la que contiene `M1`,
   `L1`, `C1` y `R1`; no se debe usar la medicion del ACS712 que este en la
   rama marcada como `BOOST`.

   Conectar sus alimentaciones: `V+` a 5 V, `V-` a COM/GND, `Out` al nodo
   `ACS_OUT` y `Filter` a un capacitor hacia COM si se desea filtrado externo.

   El modelo `ACS712.lib` corresponde al sensor de 5 A. Su salida tiene un
   offset de 2.5 V y una sensibilidad de 0.185 V/A:

```text
V(ACS_OUT) = 2.5 + 0.185*I(A)
```

   Si la pendiente aparece invertida, intercambiar `I+` e `I-`.

4. Crear una referencia `IREF` expresada en voltios del sensor. Para 1 A:

```spice
.param IREF_A=1
.param Kp=0.20
.param Ki=400
.param ACS_GAIN=0.185
VREF IREF 0 {2.5+ACS_GAIN*IREF_A}
```

5. Añadir una fuente comportamental para el error usando la salida del
   ACS712. No comparar directamente amperios con voltios, porque el sensor
   incluye el offset de 2.5 V:

```spice
BERR err 0 V={V(IREF)-V(ACS_OUT)}
```

6. Añadir el PI y limitar su salida para que nunca genere un duty negativo ni
   mayor que 100 %. La siguiente expresion es una primera prueba en LTspice:

```spice
BCTRL duty 0 V={limit(0.58+Kp*V(err)+Ki*idt(V(err)),0.02,0.95)}
```

El termino `0.58` es solo el duty inicial del circuito actual. El termino
integral corrige el error estacionario. Para evitar una condicion inicial
excesiva, se puede iniciar con `Ki=0` y activar despues la integral.

7. Crear la senal logica para `INA` usando la misma rampa `SAW` que ya existe:

```spice
BINA INA 0 V={5*(V(duty)>V(SAW))}
```

Conectar `INA` de `U1` al nodo `INA` de esta fuente comportamental y dejar
`INB` sin cambios. `INA` debe tener referencia comun con el `GND` logico del
driver. El UCC21520 se encarga del retardo, los tiempos de subida/bajada y el
dead-time; el PI no debe intentar generarlos.

## Referencia de corriente

La referencia puede ser una fuente DC o una señal variable, siempre en la
escala de tension del ACS712:

```spice
VREF IREF 0 {2.5+ACS_GAIN*IREF_A}
```

Ejemplos:

```spice
; Corriente constante de 1 A
.param IREF_A=1

; Escalon entre 0.8 A y 1.2 A
VREF IREF 0 PULSE(2.648 2.722 5m 1u 1u 5m 10m)
```

`I(L1)` se puede observar como comprobacion adicional, pero la variable del
lazo debe ser `V(ACS_OUT)`. El ACS712 debe estar en la rama cuya corriente se
desea regular.

## Como obtener una corriente estable

La corriente instantanea del inductor no sera perfectamente plana: siempre
existira un rizado triangular debido a la conmutacion. La meta realista es
regular su valor medio. Para observarlo correctamente:

- Graficar `I(L1)` y tambien su promedio en varios periodos de conmutacion.
- Mantener el buck en conduccion continua; una corriente de referencia muy
  baja puede llevarlo a conduccion discontinua.
- El modelo del ACS712 ya incluye un filtro interno (`R2=1.7 kOhm` y
   `C1=1.3 uF`), cuya frecuencia de corte es aproximadamente 72 Hz. Es
   adecuado para regular la corriente media, pero limita la rapidez del lazo.
   Si se necesita mas filtrado, agregarlo despues de `Out`:

```spice
* Filtro RC externo opcional: fc ~= 2 kHz
RIMON ACS_OUT imon 1k
CIMON imon 0 79.6n
BERR err 0 V={V(IREF)-V(imon)}
```

La frecuencia de corte aproximada es $f_c=1/(2\pi RC)=2$ kHz. Usar una
frecuencia de corte bastante menor que 20 kHz, pero mayor que la dinamica
deseada del lazo. Si se necesita otra escala de sensor, ajustar `IGAIN`.

## Ajuste del PI

1. Comenzar con `Ki=0` y un `Kp` pequeno, por ejemplo `0.05`.
2. Aumentar `Kp` hasta que la corriente siga la referencia sin oscilaciones
   sostenidas.
3. Aumentar `Ki` gradualmente hasta eliminar el error medio.
4. Si aparecen oscilaciones, reducir `Ki` primero y luego `Kp`.
5. Mantener el duty limitado entre `0.02` y `0.95` para impedir saturacion
   del convertidor y perdida de control.

Los valores `Kp=0.20` y `Ki=400` son solamente un punto de partida de
simulacion. Deben reajustarse con la tension de entrada, la carga, `L1`, la
frecuencia de conmutacion y la escala real del sensor.

## Verificacion en LTspice

Usar una simulacion transitoria de al menos 10 ms y observar:

```text
V(INA)       PWM logico de 0/5 V
V(duty)      salida del PI, entre 0 y 1 V
V(SAW)       rampa de 0 a 1 V
`V(ACS_OUT)`   salida del ACS712, 2.5 V + 0.185 V/A
`V(IREF)`      referencia en la misma escala del ACS712
V(Vmed)      tension medida, si tambien se conserva el lazo de tension
```

La comprobacion principal es que `V(ACS_OUT)` siga a `V(IREF)` y que `V(INA)`
conserve 20 kHz, cambiando unicamente su duty. Para 1 A, la medicion debe
acercarse a 2.685 V.

## Nota sobre tension de salida

Un lazo de corriente por si solo no garantiza una tension de salida fija ante
cambios de carga. Para regular simultaneamente la tension, se recomienda una
estructura de dos lazos: el lazo externo de tension genera `IREF` y el lazo
interno de corriente genera el duty. En la primera puesta a punto conviene
probar solo el lazo interno de corriente.

## Problemas de convergencia

El modelo del `UCC21520_TRANS` y los diodos pueden exigir pasos de tiempo muy
pequenos. Si la simulacion no converge:

```spice
.options method=gear reltol=0.01 abstol=1u vntol=1m plotwinsize=0
.tran 0 10m 0 50n uic
```

Primero verificar el control con un modelo ideal del interruptor o con el
driver desconectado. Despues volver a activar el `UCC21520_TRANS` y reducir
el `max timestep` solo si es necesario.