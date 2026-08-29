**4.1 Optoacoplador y Mosfet**

![](data:image/png;base64...)

![](data:image/jpeg;base64...)

**Figura 1.** Medida en la salida del microcontrolador.

![](data:image/jpeg;base64...)

**Figura 2.** Medida en la salida del optoacoplador

A primera vista, ambas mediciones aparentan tener la misma forma de onda. Sin embargo, al hacer un zoom temporal, se puede evidenciar claramente que a la salida del optoacoplador los transitorios no son instantáneos, toma alrededor de 10 µs, para este caso donde se está trabajando a 5KHz, equivalente a un periodo de 200 µs.

![](data:image/jpeg;base64...)

**Figura 3.** Transitorio en la salida del optoacoplador.

Esto ocurre debido al almacenamiento de portadores de carga, que ocurre porque cuando el LED interno se enciende, satura la base del fototransistor con electrones y huecos. Al apagarse el LED, estos portadores de carga no desaparecen instantáneamente; requieren un tiempo para recombinarse, manteniendo el transistor encendido un momento más, y el efecto Miller. Este último hace referencia a la capacitancia Base-Colector en los transistores internos del optoacoplador.

![](data:image/jpeg;base64...)

**Figura 4.** Medida a la salida del CD40106.

El componente **CD40106** es un inversor Schmidt Trigger que funciona cambiando su estado lógico cuando se alcanza un umbral determinado, el cual depende de su polarización. Este componente es útil para eliminar el tiempo de transición que queda en la señal después del optoacoplador, reduciendo el consumo de potencia en el MOSFET, evitando errores de sincronización y ayudando a prevenir ruido.

En la **figura 5** se puede ver más claramente el disparo en el estado de la señal una vez se llega al umbral en el tiempo de transición, haciendo los cambios lógicos más instantáneos.

![](data:image/jpeg;base64...)

**Figura 5.** Comparación de transitorios antes y después del Schmidt Trigger.

![](data:image/jpeg;base64...)

**Figura 6.** PWM en el gate-source.

En comparación con la gráfica obtenida justo después del CD40106, no se evidencia una alteración a la señal. Esto debido a que el arreglo entre Q2 y Q3 no aporta un cambio a la onda que pasa a través de estos.

Este juego de transistores conectados de tal manera se les conoce como Totem-Pole, sirve principalmente como un driver de corriente ultra rápido, o búfer, y está diseñado para activar y desactivar conmutadores de potencia. En este caso, el transistor MOSFET IRFZ44N. El transistor Q2 (NPN) se activa para entregar un gran pico de corriente desde la fuente de alimentación (Vcc) cargando la compuerta del MOSFET, en cuestión de nanosegundos. Por otro lado, Q3 (PNP) se activa para drenar la carga de la capacitancia del gate, a tierra, y así poder cambiar de estado más rápidamente, apagando el dispositivo.

4.2 OPTODRIVER e IGBT

\* Cuando se utiliza un optodriver no es necesario el arreglo de Totem-Pole porque internamente tiene integrado un arreglo similar de push-pull. En otras palabras, está diseñado especialmente para manejar compuertas de potencia, además del aislamiento óptico.

Preguntas del informe:

* el aislamiento galvánico entre las etapas de control y potencia se implementa fundamentalmente para proteger la electrónica de control contra sobrevoltajes destructivos, eliminar el ruido electromagnético y los bucles de tierra ypermitir el manejo de transistores en configuración flotante (rama alta).
* La idea de incluir un opto acoplador o un optodriver en un circuito de potencia, es precisamente la descrita anteriormente, proteger la parte de control, la cual suele ser más costosa. Por lo tanto, esto exige que hayan puntos de referencia (tierras) aisladas, para que los circuitos de la parte de control, y de potencia, no se afecten el uno al otro, que por el contrario, sólo se comuniquen a través de la señal óptica.
* La lógica Transistor-Transistor (TTL) es una familia de circuitos integrados digitales basada en transistores bipolares (BJT) y resistencias para ejecutar operaciones lógicas. Operando bajo una fuente de alimentación estándar de 5 V, define sus estados lógicos en rangos de voltaje específicos, típicamente entre 0 V y 0.8 V para el nivel bajo (LOW) y entre 2 V y 5 V para el nivel alto (HIGH). Su arquitectura destaca por el uso de una etapa de salida en configuración Totem-Pole discreta o integrada, la cual proporciona una baja impedancia de salida que optimiza la velocidad de conmutación y mejora la capacidad de manejo de corriente. Popularizada a través de la serie comercial 7400, la tecnología TTL representó un avance significativo en velocidad frente a familias previas como RTL o DTL, aunque presenta un consumo de potencia mayor en comparación con la tecnología CMOS contemporánea.
* La tecnología CMOS es la arquitectura dominante en la fabricación de circuitos digitales y microprocesadores modernos, fundamentada en el uso combinado y simétrico de MOSFETs de canal P y canal N. Su principal ventaja radica en su consumo de potencia estática extremadamente bajo, ya que en cualquier estado lógico constante (alto o bajo) uno de los dos MOSFETs complementarios permanece apagado, cortando el paso de corriente directa entre la fuente y tierra. Esta familia opera en un amplio rango de voltajes de alimentación (típicamente de 3 V a 15 V en series tradicionales como la 4000, o de 1.2 V a 5 V en familias avanzadas) y ofrece una alta inmunidad al ruido junto con una elevada impedancia de entrada, lo que permite conectar múltiples entradas a una sola salida sin degradar la señal. Aunque su consumo de potencia incrementa de forma proporcional a la frecuencia de conmutación debido a la carga y descarga de las capacitancias internas, supera sustancialmente a la tecnología TTL en densidad de integración y eficiencia energética.