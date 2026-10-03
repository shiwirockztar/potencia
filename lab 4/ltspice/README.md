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
  +---------------- I(L1), corriente del buck <- potencia
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
2. Conservar `SAW` (`V4`) y la frecuencia de 20 kHz.
3. Crear un nodo de medida de corriente para el inductor del buck. La forma
   mas sencilla, sin colocar un sensor fisico, es usar directamente `I(L1)`.
4. Crear una referencia `IREF`. Para el ejemplo se usa una escala de 1 V/A:

```spice
.param IREF_A=1
.param Kp=0.20
.param Ki=400
.param IGAIN=1
```

5. Añadir una fuente comportamental para el error. Si la orientacion de
   `L1` hace que `I(L1)` sea negativa, cambiar el signo de esa corriente:

```spice
BERR err 0 V={V(IREF)-IGAIN*I(L1)}
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

La referencia puede ser una fuente DC o una señal variable:

```spice
VREF IREF 0 {IREF_A}
```

Ejemplos:

```spice
; Corriente constante de 1 A
.param IREF_A=1

; Escalon entre 0.8 A y 1.2 A
VREF IREF 0 PULSE(0.8 1.2 5m 1u 1u 5m 10m)
```

Si se desea regular la corriente de salida en lugar de la corriente del
inductor, se debe medir la corriente en la rama de carga. En un buck CCM,
`I(L1)` es una buena variable para el lazo interno, pero contiene el rizado de
conmutacion.

## Como obtener una corriente estable

La corriente instantanea del inductor no sera perfectamente plana: siempre
existira un rizado triangular debido a la conmutacion. La meta realista es
regular su valor medio. Para observarlo correctamente:

- Graficar `I(L1)` y tambien su promedio en varios periodos de conmutacion.
- Mantener el buck en conduccion continua; una corriente de referencia muy
  baja puede llevarlo a conduccion discontinua.
- Si el PI reacciona al rizado, filtrar la medicion antes de `BERR`. Un filtro
  inicial puede ser:

```spice
EIMON imon 0 LAPLACE={IGAIN*I(L1)} {1/(1+s/(2*pi*2000))}
BERR err 0 V={V(IREF)-V(imon)}
```

Usar una frecuencia de corte bastante menor que 20 kHz, pero mayor que la
dinamica deseada del lazo. Si esta expresion no es aceptada por la version de
LTspice, se puede reemplazar por un `UniversalOpAmp` con una red RC de paso
bajo.

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
I(L1)        corriente instantanea del inductor
V(IREF)      corriente de referencia en la escala elegida
V(Vmed)      tension medida, si tambien se conserva el lazo de tension
```

La comprobacion principal es que el promedio de `I(L1)` siga `IREF` y que
`V(INA)` conserve 20 kHz, cambiando unicamente su duty.

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