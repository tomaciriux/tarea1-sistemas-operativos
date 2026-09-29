# Tarea 1: Planificador Dieciochero
Sistemas Operativos — Universidad Diego Portales

Integrantes:
- Tomás Hernández
- Renato Villavicencio


## 1. Introducción y Descripción General

En este trabajo desarrollamos el Planificador Dieciochero, un programa en lenguaje C encargado de planificar y ejecutar actividades concurrentes modeladas a través de un Grafo Acíclico Dirigido (DAG).

El sistema se encarga de leer el archivo de actividades, verificar que no existan dependencias circulares, lanzar los procesos respetando un límite máximo de concurrencia K, comunicar los resultados mediante tuberías (pipes) y manejar de forma segura tanto los errores en actividades como la interrupción del usuario mediante la señal SIGINT (Ctrl+C).


## 2. Explicación de Funciones Implementadas

Para el desarrollo del proyecto se definieron dos estructuras principales. La primera es Actividad, que almacena la información de cada tarea (su identificador, nombre, duración, listas dinámicas de dependencias y su estado actual). La segunda es Proceso, que lleva el registro de los procesos hijos que se encuentran corriendo en el sistema, guardando su PID y el descriptor de lectura de su tubería.

El flujo del programa se organiza a través de las siguientes funciones:

La función leer_linea se encarga de parsear cada línea del archivo de entrada, separando el ID, nombre, tiempo y la lista de dependencias. Si una tarea no especifica tiempo, le asigna un valor aleatorio entre 100 y 5000 milisegundos.

La función cargar_plan abre el archivo de texto, reserva la memoria dinámica para las tareas (duplicando su capacidad si es necesario) y construye el grafo de dependencias, validando que no existan IDs duplicados ni dependencias hacia actividades que no existan.

La función hay_ciclo implementa el ordenamiento topológico mediante el algoritmo de Kahn para verificar que el grafo sea válido antes de iniciar cualquier ejecución.

La función iniciar_actividad crea la tubería con pipe(), bifurca el proceso padre mediante fork() y registra al hijo dentro de la tabla de procesos activos.

La función ejecutar_hijo es la que corre dentro del proceso hijo, simulando la duración de la tarea con nanosleep() y enviando un mensaje con su estado a través de la tubería antes de finalizar.

La función abortar_dependientes realiza un recorrido en anchura (BFS) para marcar como abortadas únicamente aquellas tareas que dependían de una actividad que falló.

La función manejar_sigint se encarga de capturar la señal SIGINT (Ctrl+C) de forma segura a través de una bandera atómica, permitiendo que el planificador aborte ordenadamente todos los procesos antes de salir.


## 3. Justificación de Decisiones de Diseño

En primer lugar, para coordinar los procesos sin infringir la prohibición de usar hilos (threads) y sin incurrir en busy-waiting, se utilizó la llamada al sistema poll() sobre los descriptores de lectura de los pipes de los procesos activos. De esta manera, el proceso padre no consume ciclos innecesarios de CPU mientras espera que los hijos terminen.

En segundo lugar, se optó por el uso de arreglos dinámicos con redimensionamiento exponencial mediante realloc, lo que permite que el planificador pueda soportar pruebas de estrés con gran cantidad de actividades sin desbordar la memoria.

En tercer lugar, la comunicación entre procesos se diseñó mediante tuberías anónimas con escrituras atómicas de mensajes de texto (OK o FAIL), lo que garantiza que la información se transmita de forma íntegra e independiente.

Finalmente, el manejo de errores y de la señal SIGINT se estructuró para no cerrar el programa de forma abrupta, sino asegurando la liberación de recursos, la cancelación de procesos activos mediante señales y el aislamiento de las ramas del grafo afectadas.


## 4. Modo de Compilación y Uso

Para compilar el proyecto con las banderas estrictas solicitadas:

make

Para ejecutar el planificador:

./planificador plan.txt 3

Para simular fallos en actividades específicas (por ejemplo, actividades 2 y 4):

FAIL_IDS="2,4" ./planificador plan.txt 3

Para limpiar los archivos binarios compilados:

make clean
