Informe de diseño y simulación de un convertidor Boost

1. Objetivo

Diseñar y simular un convertidor elevador de tensión (Boost Converter) capaz de elevar una tensión de entrada de 10 V DC hasta aproximadamente 17 V DC, entregando una potencia de aproximadamente 15 W, con una frecuencia de conmutación de 20 kHz.

El diseño busca mantener un rizado de tensión inferior o igual al 5 % y posteriormente llevar el circuito a una implementación física en una board.

---

2. Especificaciones iniciales

Parámetro| Valor
Tensión de entrada (V_{in})| 10 V
Tensión de salida (V_{out})| 17 V
Potencia de salida| 15 W
Frecuencia de conmutación| 20 kHz
Rizado máximo especificado| 5 %
Carga utilizada| 19.3 Ω
Inductor| 2.7 mH
Condensador| 107 µF

La resistencia de carga se calculó a partir de:

[
R=\frac{V_{out}^{2}}{P_{out}}
]

[
R=\frac{17^2}{15}\approx19.27,\Omega
]

Por lo tanto, se seleccionó un valor comercial aproximado de:

[
\boxed{R=19.3,\Omega}
]

---

3. Cálculo del ciclo de trabajo

Para un convertidor Boost ideal se utiliza:

[
V_{out}=\frac{V_{in}}{1-D}
]

Despejando el ciclo de trabajo:

[
D=1-\frac{V_{in}}{V_{out}}
]

Sustituyendo:

[
D=1-\frac{10}{17}
]

[
\boxed{D\approx0.412}
]

Por lo tanto:

[
\boxed{D\approx41.2%}
]

Este fue el valor utilizado como referencia para la señal PWM.

---

4. Frecuencia y señal PWM

La frecuencia de conmutación seleccionada fue:

[
f_s=20,kHz
]

El período correspondiente es:

[
T=\frac{1}{f_s}
]

[
T=\frac{1}{20000}=50,\mu s
]

Para un duty cycle de 41.2 %, el tiempo de conducción aproximado es:

[
T_{ON}=0.412(50,\mu s)
]

[
\boxed{T_{ON}\approx20.6,\mu s}
]

En LTspice se utilizó finalmente una fuente PWM del tipo:

PULSE(0 10 0 10n 10n 20.6u 50u)

Esta configuración genera aproximadamente:

- Nivel bajo: 0 V
- Nivel alto: 10 V
- Frecuencia: 20 kHz
- Período: 50 µs
- Tiempo ON: 20.6 µs
- Duty cycle: aproximadamente 41.2 %

---

5. Cálculo de la corriente de salida

La corriente nominal de salida se obtiene mediante:

[
I_{out}=\frac{P_{out}}{V_{out}}
]

[
I_{out}=\frac{15}{17}
]

[
\boxed{I_{out}\approx0.882,A}
]

La corriente de entrada ideal aproximada es:

[
I_{in}=\frac{P_{out}}{V_{in}}
]

[
I_{in}=\frac{15}{10}
]

[
\boxed{I_{in}\approx1.5,A}
]

En un circuito real esta corriente será algo mayor debido a las pérdidas del MOSFET, diodo, inductor y demás componentes.

---

6. Selección de la bobina

Inicialmente se consideró un rizado de corriente del 5 %.

Tomando:

[
\Delta I_L=0.05(1.5)
]

[
\Delta I_L=0.075,A
]

Para el Boost:

[
L=\frac{V_{in}D}{f_s\Delta I_L}
]

Sustituyendo:

[
L=\frac{10(0.412)}
{20000(0.075)}
]

[
L\approx2.75,mH
]

Se seleccionó el valor comercial:

[
\boxed{L=2.7,mH}
]

---

7. Selección del condensador

Inicialmente se obtuvo un valor teórico cercano a 21.4 µF considerando un rizado de tensión del 5 %.

Sin embargo, durante el desarrollo de la simulación se decidió utilizar:

[
\boxed{C=107,\mu F}
]

El valor de 107 µF produce un rizado considerablemente menor que el máximo permitido, aunque aumenta el tiempo necesario para alcanzar el régimen permanente.

Por esta razón, se decidió mantener este capacitor para el desarrollo actual del proyecto.

---

8. Resultado del rizado de tensión

Con el condensador de aproximadamente 100–107 µF, en la simulación se observó una tensión aproximadamente entre:

[
V_{max}=17.15,V
]

y

[
V_{min}=17.01,V
]

Por lo tanto, el rizado pico a pico es:

[
\Delta V=17.15-17.01
]

[
\Delta V=0.14,V
]

El porcentaje de rizado respecto a 17 V es aproximadamente:

[
%Ripple=\frac{0.14}{17}(100)
]

[
\boxed{%Ripple\approx0.82%}
]

Por lo tanto, el resultado obtenido es considerablemente menor que el límite establecido del 5 %.

Esto indica que el condensador seleccionado proporciona una salida bastante estable, aunque a costa de un mayor tiempo de establecimiento.

---

9. Componentes utilizados en la simulación

Durante el desarrollo se utilizaron los siguientes componentes:

MOSFET

Se seleccionó finalmente:

[
\boxed{\text{IRFZ44N}}
]

El MOSFET debe ser de canal N, ya que el Boost implementado utiliza una configuración convencional de conmutación por el lado bajo.

Diodo

Se utilizó:

[
\boxed{\text{1N5819}}
]

El 1N5819 es un diodo Schottky, apropiado para una primera simulación debido a su baja caída de tensión y características de conmutación.

Para la implementación física se deberá verificar cuidadosamente la tensión inversa máxima, corriente y disipación. También puede considerarse un Schottky con mayor margen, dependiendo del componente disponible.

---

10. Problemas encontrados durante la simulación

Durante el desarrollo se presentaron varios problemas que fueron solucionados progresivamente.

10.1. Selección incorrecta del tipo de MOSFET

Inicialmente se utilizó un MOSFET de canal P cuando el diseño requería un MOSFET de canal N.

Esto provocaba que el convertidor no funcionara correctamente y que la tensión del capacitor permaneciera muy por debajo de los 17 V.

Corrección:

Se cambió el dispositivo por un MOSFET de canal N, finalmente utilizando el IRFZ44N.

---

10.2. Configuración incorrecta del PWM

En una primera configuración se utilizó una señal similar a:

PULSE(0 24 0 10n 25u 50u)

Esta señal no correspondía al ciclo de trabajo calculado y además utilizaba una tensión de compuerta innecesariamente elevada.

Corrección:

Se estableció:

PULSE(0 10 0 10n 10n 20.6u 50u)

Esta configuración corresponde aproximadamente al 41.2 % de duty requerido.

---

10.3. Tiempo de simulación insuficiente

Inicialmente se utilizó una simulación de aproximadamente 10 ms. Debido al uso de un capacitor de 107 µF, la salida necesitaba más tiempo para alcanzar el régimen permanente.

Se observó que la tensión continuaba estabilizándose después de los 10 ms.

Corrección:

Se aumentó el tiempo de simulación, por ejemplo:

.tran 30m

Esto permite observar con mayor claridad el proceso de arranque y la estabilización de la tensión de salida.

---

10.4. Valores iniciales de tensión inesperados

Durante las primeras pruebas se observaron tensiones de salida muy inferiores a las esperadas, llegando incluso a valores cercanos a 0.3 V y posteriormente alrededor de 11.2 V.

La causa principal estaba relacionada con la configuración del MOSFET y posteriormente con la señal de control.

Después de corregir el tipo de MOSFET y la señal PWM, el circuito comenzó a elevar correctamente la tensión.

---

11. Cálculo de potencia mediante LTspice

Para comprobar la potencia se utilizó la medición de voltaje y corriente proporcionada por LTspice.

La potencia instantánea puede obtenerse mediante:

[
p(t)=v(t)i(t)
]

Para la carga:

[
P_{out}=V_{out}I_{out}
]

Con aproximadamente 17 V y una resistencia de 19.3 Ω:

[
P_{out}=\frac{17^2}{19.3}
]

[
P_{out}\approx14.97,W
]

Este resultado es muy cercano a los 15 W especificados, por lo que la resistencia seleccionada representa adecuadamente la carga requerida.

---

12. Factor de potencia y eficiencia

Es importante diferenciar estos dos conceptos.

El factor de potencia (FP) no es equivalente a la eficiencia.

La eficiencia se calcula como:

[
\eta=\frac{P_{out}}{P_{in}}\times100%
]

En este proyecto, al tratarse de una entrada de 10 V DC, el concepto de factor de potencia no se aplica de la misma forma que en un sistema alimentado directamente desde una red AC.

Por lo tanto, para evaluar el desempeño del Boost será más importante determinar:

- Potencia de entrada.
- Potencia de salida.
- Pérdidas.
- Eficiencia.
- Rizado de tensión.
- Rizado de corriente.

---

13. Esquema funcional del convertidor

La estructura básica utilizada es:

             L = 2.7 mH
 +10 V ─────UUUU─────●──────|>|──────●──── +17 V
                     │       D        │
                     │                │
                   D │                C
                 MOSFET            107 µF
                   S │                │
                     │              19.3 Ω
                    GND               │
                                     GND

El MOSFET es controlado mediante una señal PWM de aproximadamente 20 kHz y 41.2 % de duty cycle.

---

14. Consideraciones para la implementación en board

Antes de pasar de la simulación al circuito físico se deben verificar varios aspectos.

Inductor

La bobina debe soportar una corriente superior a la corriente máxima que circulará por ella.

No basta con que tenga un valor de 2.7 mH; también se debe comprobar:

- Corriente de saturación.
- Resistencia serie.
- Corriente máxima permitida.
- Pérdidas.

Condensador

El condensador debe tener una tensión nominal superior a los 17 V.

Se recomienda dejar margen, por ejemplo utilizando un condensador con una tensión nominal de al menos 25 V, preferiblemente mayor dependiendo del diseño final.

También se debe verificar su corriente de rizado.

MOSFET

Para el IRFZ44N se debe verificar:

- (V_{DS}) máximo.
- Corriente máxima.
- (R_{DS(on)}).
- Disipación térmica.
- Tensión (V_{GS}).
- Necesidad de disipador.

Además, en la implementación física será recomendable colocar una resistencia entre el PWM y Gate y una resistencia de pull-down Gate-Source para garantizar que el MOSFET permanezca apagado cuando no exista señal de control.

Diodo

Se debe verificar:

- Tensión inversa máxima.
- Corriente directa.
- Corriente de recuperación.
- Disipación térmica.

Para una frecuencia de 20 kHz, un diodo Schottky es una alternativa conveniente.

Cableado y conexiones

Las conexiones de potencia deben ser cortas y con conductores adecuados para la corriente del circuito.

La trayectoria:

condensador → MOSFET → bobina → diodo → condensador

debe mantenerse lo más compacta posible para reducir inductancias parásitas y picos de tensión.

---

15. Resultados obtenidos hasta el momento

El circuito simulado ha conseguido alcanzar aproximadamente la tensión objetivo:

[
\boxed{V_{out}\approx17V}
]

con una potencia de carga cercana a:

[
\boxed{P_{out}\approx15W}
]

y un rizado observado de:

[
\boxed{\approx0.82%}
]

lo cual se encuentra por debajo del límite del 5 %.

La frecuencia de conmutación se mantiene en:

[
\boxed{f_s=20,kHz}
]

y el ciclo de trabajo utilizado es aproximadamente:

[
\boxed{D=41.2%}
]

---

16. Trabajo pendiente

Antes de realizar la implementación física se recomienda:

1. Confirmar la tensión y corriente reales de entrada.
2. Medir la potencia de entrada en LTspice.
3. Calcular la eficiencia.
4. Verificar el rizado de corriente de la bobina.
5. Verificar la corriente máxima del MOSFET.
6. Verificar la corriente y tensión inversa del diodo.
7. Confirmar la corriente de saturación del inductor.
8. Verificar la potencia disipada por el MOSFET.
9. Seleccionar correctamente los componentes físicos.
10. Implementar el circuito en board.
11. Realizar pruebas inicialmente con una fuente de alimentación limitada en corriente.
12. Medir (V_{in}), (V_{out}), corriente de entrada, corriente de salida y temperatura de los componentes.
13. Comparar los resultados experimentales con la simulación.

---

17. Conclusión

El diseño desarrollado corresponde a un convertidor Boost que eleva una entrada de 10 V DC hasta aproximadamente 17 V, con una potencia de salida objetivo de 15 W y una frecuencia de conmutación de 20 kHz.

El ciclo de trabajo calculado inicialmente fue de aproximadamente 41.2 %, mientras que los valores seleccionados para los elementos principales fueron 2.7 mH para la bobina y 107 µF para el condensador.

Durante la simulación se presentaron problemas relacionados principalmente con la selección del tipo de MOSFET y la configuración de la señal PWM. Estos problemas fueron identificados y corregidos, permitiendo obtener finalmente una tensión de salida cercana a los 17 V.

El uso del condensador de 107 µF proporciona un rizado de tensión considerablemente inferior al límite del 5 %, aunque aumenta el tiempo de establecimiento del sistema.

Los resultados obtenidos hasta el momento indican que el diseño es viable para continuar con el análisis de eficiencia, selección definitiva de componentes y posterior implementación física en una board.

34. Nueva etapa: implementación del controlador PID

Una vez finalizada la etapa inicial de simulación del convertidor Boost y el acondicionamiento de la señal del sensor de corriente ACS712, se decidió avanzar hacia una nueva etapa del proyecto: la implementación de un controlador PID.

El objetivo de esta etapa es desarrollar un sistema de control que permita regular la respuesta del convertidor y mantener la variable de salida en el valor de referencia establecido.

Para el proyecto se ha establecido como referencia principal:

[
V_{ref}=17V
]

La señal de realimentación deberá compararse con la referencia para obtener el error:

[
e(t)=V_{ref}-V_{out}
]

Este error será procesado por las diferentes acciones del controlador PID.

---

35. Estructura del controlador PID

El controlador PID está compuesto por tres acciones:

Acción proporcional

La acción proporcional responde al error instantáneo:

[
u_P(t)=K_Pe(t)
]

Esta parte permite aumentar o disminuir la acción de control dependiendo de la magnitud del error.

Acción integral

La acción integral acumula el error a lo largo del tiempo:

[
u_I(t)=K_I\int e(t)dt
]

Su función principal es eliminar el error permanente o error en estado estacionario.

Es importante destacar que la acumulación del error pertenece a la acción integral y no a la derivativa.

Acción derivativa

La acción derivativa responde a la velocidad de cambio del error:

[
u_D(t)=K_D\frac{de(t)}{dt}
]

Esta acción permite anticipar cambios en el sistema y puede ayudar a reducir sobreimpulsos y mejorar la estabilidad.

---

36. Problema encontrado durante la implementación del PID

Durante la implementación de la etapa PID se presentó una dificultad relacionada con la conexión de la parte derivativa.

Se observó que el error parecía acumularse durante la simulación.

Inicialmente se consideró que este comportamiento estaba relacionado con la etapa derivativa.

Sin embargo, se identificó que debe diferenciarse entre las funciones de las acciones integral y derivativa:

- La integral acumula el error.
- La derivativa calcula la variación del error respecto al tiempo.

Por lo tanto, la acumulación observada debe analizarse principalmente en la etapa integral y en la forma en que se está generando el error.

---

37. Ubicación de la etapa derivativa

La etapa derivativa debe recibir como entrada la señal de error del controlador:

[
e(t)=V_{ref}-V_{out}
]

Por lo tanto, conceptualmente la conexión debe ser:

Vref ───────┐
            ▼
         RESTADOR
            ▲
            │
           Vout
            │
            ▼
          e(t)
            │
      ┌─────┼─────┐
      │     │     │
      ▼     ▼     ▼
     P      I      D
      │     │      │
      └─────┼──────┘
            ▼
          SUMADOR
            │
            ▼
       Señal de control

La salida del operacional correspondiente a la parte derivativa debe conectarse posteriormente al sumador del PID, junto con las salidas proporcional e integral.

No debe conectarse directamente como si fuera una señal de compuerta para el MOSFET.

---

38. Relación entre el PID y el UCC21520

El UCC21520 será utilizado posteriormente como controlador de compuerta del MOSFET.

Por lo tanto, el PID no debe conectarse directamente a la compuerta.

La cadena prevista es:

Vref
 │
 ▼
Comparador / Restador
 │
 ▼
Error
 │
 ├────► P
 ├────► I
 └────► D
 │
 ▼
Sumador PID
 │
 ▼
Control PWM
 │
 ▼
UCC21520
 │
 ▼
IRFZ44N
 │
 ▼
Convertidor Boost
 │
 ▼
Vout
 │
 └──────────────► Realimentación

Esto permite separar correctamente las funciones de control, driver de compuerta y etapa de potencia.

---

39. Importancia de la señal de realimentación

Para un control de tensión, la variable que debe utilizarse para formar el error principal es la tensión de salida.

Por lo tanto:

[
e(t)=17V-V_{out}
]

El ACS712, por su parte, proporciona información sobre la corriente.

Su señal puede utilizarse posteriormente para:

- Control de corriente.
- Limitación de corriente.
- Protección contra sobrecorriente.
- Implementación de un lazo interno de corriente.

Por esta razón, no se debe asumir que la salida del ACS712 debe ser directamente la señal de error del PID de tensión.

---

40. Estado actual de esta nueva etapa

Hasta el momento se ha iniciado la implementación del controlador PID mediante amplificadores operacionales.

La etapa se encuentra en desarrollo, especialmente en lo relacionado con:

- Generación correcta del error.
- Implementación de la acción proporcional.
- Implementación de la acción integral.
- Implementación de la acción derivativa.
- Sumatoria de las tres acciones.
- Conversión de la señal de control en PWM.
- Integración posterior con el UCC21520.

Todavía no se debe considerar finalizada esta etapa hasta comprobar que el controlador responde correctamente ante cambios en la tensión de salida.

---

41. Consideraciones para la implementación del PID

Antes de conectar el PID al UCC21520 se deben comprobar los siguientes puntos:

1. Confirmar que la referencia sea de 17 V.
2. Confirmar que la señal de realimentación corresponda realmente a (V_{out}).
3. Verificar la polaridad del error:

[
e(t)=V_{ref}-V_{out}
]

4. Comprobar que la acción integral no produzca una acumulación excesiva.
5. Verificar que la acción derivativa esté conectada a la señal de error.
6. Comprobar que el sumador combine correctamente P, I y D.
7. Evitar saturación de los amplificadores operacionales.
8. Establecer límites para la señal de control.
9. Generar posteriormente un PWM compatible con el UCC21520.
10. Comprobar la señal de compuerta del IRFZ44N antes de conectar nuevamente la etapa de potencia.

---

42. Próximo objetivo

El siguiente objetivo del proyecto es finalizar la implementación del controlador PID en LTspice y verificar su comportamiento antes de conectar el UCC21520.

La secuencia de pruebas recomendada es:

[
\boxed{\text{Error}\rightarrow P/I/D\rightarrow\text{Sumador}\rightarrow PWM}
]

y posteriormente:

[
\boxed{PWM\rightarrow UCC21520\rightarrow IRFZ44N}
]

Finalmente se deberá cerrar el lazo mediante la realimentación de la tensión de salida y comprobar que el convertidor mantiene aproximadamente:

[
\boxed{V_{out}=17V}
]

ante las condiciones de carga establecidas.

---

43. Observación importante

La implementación del PID debe realizarse por etapas. No se recomienda conectar simultáneamente el sensor, los amplificadores, el PID, el PWM y el UCC21520 sin comprobar individualmente cada bloque.

Se recomienda validar en este orden:

1. Error de tensión → 2. P → 3. I → 4. D → 5. Sumador → 6. PWM → 7. UCC21520 → 8. MOSFET → 9. Boost.

De esta manera, cualquier comportamiento inesperado podrá asociarse con una etapa específica y será más sencillo realizar las correcciones correspondientes.
