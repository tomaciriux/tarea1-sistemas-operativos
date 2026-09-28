#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

#define MAX_ID 32
#define MAX_NOMBRE 64
#define MAX_MENSAJE 128

typedef enum {
    PENDIENTE,
    LISTA,
    CORRIENDO,
    TERMINADA,
    FALLIDA,
    ABORTADA
} Estado;

typedef struct {
    char id[MAX_ID];
    char nombre[MAX_NOMBRE];
    int tiempo;

    int *dependencias;
    int cantidad_dependencias;

    int *dependientes;
    int cantidad_dependientes;

    int pendientes;
    Estado estado;
    pid_t pid;
} Actividad;

typedef struct {
    int actividad;
    pid_t pid;
    int pipe_lectura;
} Proceso;

static Actividad *actividades = NULL;
static int cantidad = 0;

static char *quitar_espacios(char *texto) {
    while (*texto == ' ' || *texto == '\t' || *texto == '\n' || *texto == '\r')
        texto++;

    char *fin = texto + strlen(texto);
    while (fin > texto && (fin[-1] == ' ' || fin[-1] == '\t' ||
                           fin[-1] == '\n' || fin[-1] == '\r'))
        *--fin = '\0';

    return texto;
}

static int buscar_actividad(const char *id) {
    for (int i = 0; i < cantidad; i++) {
        if (strcmp(actividades[i].id, id) == 0)
            return i;
    }
    return -1;
}

static int agregar_dependencia(Actividad *a, int indice) {
    int nuevo = a->cantidad_dependencias + 1;
    int *tmp = realloc(a->dependencias, nuevo * sizeof(int));
    if (tmp == NULL)
        return -1;
    a->dependencias = tmp;
    a->dependencias[a->cantidad_dependencias] = indice;
    a->cantidad_dependencias = nuevo;
    return 0;
}

static int agregar_dependiente(Actividad *a, int indice) {
    int nuevo = a->cantidad_dependientes + 1;
    int *tmp = realloc(a->dependientes, nuevo * sizeof(int));
    if (tmp == NULL)
        return -1;
    a->dependientes = tmp;
    a->dependientes[a->cantidad_dependientes] = indice;
    a->cantidad_dependientes = nuevo;
    return 0;
}

static int leer_linea(char *linea, Actividad *a, char ***dependencias, int *n_dep) {
    char *campos[4];
    char *p = linea;
    int i;

    for (i = 0; i < 3; i++) {
        char *dos_puntos = strchr(p, ':');
        if (dos_puntos == NULL)
            return -1;
        *dos_puntos = '\0';
        campos[i] = quitar_espacios(p);
        p = dos_puntos + 1;
    }
    campos[3] = quitar_espacios(p);

    if (campos[0][0] == '\0' || campos[1][0] == '\0')
        return -1;
    if (strlen(campos[0]) >= MAX_ID || strlen(campos[1]) >= MAX_NOMBRE)
        return -1;

    strcpy(a->id, campos[0]);
    strcpy(a->nombre, campos[1]);

    if (campos[2][0] == '\0') {
        a->tiempo = 100 + rand() % 4901;
    } else {
        char *fin;
        long valor = strtol(campos[2], &fin, 10);
        if (*fin != '\0' || valor < 0 || valor > 2147483647L)
            return -1;
        a->tiempo = (int)valor;
    }

    *dependencias = NULL;
    *n_dep = 0;

    char *deps = campos[3];
    if (deps[0] == '[')
        deps++;

    size_t largo = strlen(deps);
    if (largo > 0 && deps[largo - 1] == ']')
        deps[largo - 1] = '\0';

    deps = quitar_espacios(deps);
    if (*deps == '\0')
        return 0;

    char *copia = strdup(deps);
    if (copia == NULL)
        return -1;

    char *token = strtok(copia, ",");
    while (token != NULL) {
        token = quitar_espacios(token);
        if (*token != '\0') {
            char **tmp = realloc(*dependencias, (*n_dep + 1) * sizeof(char *));
            if (tmp == NULL) {
                free(copia);
                return -1;
            }
            *dependencias = tmp;
            (*dependencias)[*n_dep] = strdup(token);
            if ((*dependencias)[*n_dep] == NULL) {
                free(copia);
                return -1;
            }
            (*n_dep)++;
        }
        token = strtok(NULL, ",");
    }

    free(copia);
    return 0;
}

static int cargar_plan(const char *nombre_archivo) {
    FILE *archivo = fopen(nombre_archivo, "r");
    if (archivo == NULL) {
        perror("No se pudo abrir plan.txt");
        return -1;
    }

    int capacidad = 16;
    actividades = calloc(capacidad, sizeof(Actividad));
    if (actividades == NULL) {
        fclose(archivo);
        return -1;
    }

    char ***deps_crudas = calloc(capacidad, sizeof(char **));
    int *n_deps_crudas = calloc(capacidad, sizeof(int));
    if (deps_crudas == NULL || n_deps_crudas == NULL) {
        fclose(archivo);
        free(deps_crudas);
        free(n_deps_crudas);
        return -1;
    }

    char *linea = NULL;
    size_t tam = 0;
    int numero_linea = 0;

    while (getline(&linea, &tam, archivo) != -1) {
        numero_linea++;
        char *texto = quitar_espacios(linea);

        if (*texto == '\0' || *texto == '#')
            continue;

        if (cantidad == capacidad) {
            capacidad *= 2;
            Actividad *nuevas = realloc(actividades, capacidad * sizeof(Actividad));
            char ***nuevos_deps = realloc(deps_crudas, capacidad * sizeof(char **));
            int *nuevos_n = realloc(n_deps_crudas, capacidad * sizeof(int));
            if (nuevas == NULL || nuevos_deps == NULL || nuevos_n == NULL) {
                free(nuevas);
                free(nuevos_deps);
                free(nuevos_n);
                free(linea);
                fclose(archivo);
                return -1;
            }
            actividades = nuevas;
            deps_crudas = nuevos_deps;
            n_deps_crudas = nuevos_n;
            memset(&actividades[cantidad], 0, (capacidad - cantidad) * sizeof(Actividad));
        }

        char **deps = NULL;
        int n = 0;
        if (leer_linea(texto, &actividades[cantidad], &deps, &n) != 0) {
            fprintf(stderr, "Linea %d invalida.\n", numero_linea);
            continue;
        }

        if (buscar_actividad(actividades[cantidad].id) != -1) {
            fprintf(stderr, "ID duplicado en linea %d: %s\n", numero_linea,
                    actividades[cantidad].id);
            for (int j = 0; j < n; j++)
                free(deps[j]);
            free(deps);
            continue;
        }

        deps_crudas[cantidad] = deps;
        n_deps_crudas[cantidad] = n;
        cantidad++;
    }

    free(linea);
    fclose(archivo);

    for (int i = 0; i < cantidad; i++) {
        for (int j = 0; j < n_deps_crudas[i]; j++) {
            int dep = buscar_actividad(deps_crudas[i][j]);
            if (dep == -1) {
                fprintf(stderr, "Dependencia inexistente: %s -> %s\n",
                        actividades[i].id, deps_crudas[i][j]);
                for (int k = 0; k < n_deps_crudas[i]; k++)
                    free(deps_crudas[i][k]);
                free(deps_crudas[i]);
                free(deps_crudas);
                free(n_deps_crudas);
                return -1;
            }

            if (agregar_dependencia(&actividades[i], dep) != 0 ||
                agregar_dependiente(&actividades[dep], i) != 0) {
                free(deps_crudas);
                free(n_deps_crudas);
                return -1;
            }
        }

        actividades[i].pendientes = actividades[i].cantidad_dependencias;
        actividades[i].estado = PENDIENTE;

        for (int j = 0; j < n_deps_crudas[i]; j++)
            free(deps_crudas[i][j]);
        free(deps_crudas[i]);
    }

    free(deps_crudas);
    free(n_deps_crudas);
    return cantidad > 0 ? 0 : -1;
}

static int hay_ciclo(void) {
    int *pendientes = malloc(cantidad * sizeof(int));
    int *cola = malloc(cantidad * sizeof(int));
    if (pendientes == NULL || cola == NULL) {
        free(pendientes);
        free(cola);
        return 1;
    }

    for (int i = 0; i < cantidad; i++) {
        pendientes[i] = actividades[i].pendientes;
    }

    int inicio = 0, fin = 0, procesadas = 0;
    for (int i = 0; i < cantidad; i++) {
        if (pendientes[i] == 0)
            cola[fin++] = i;
    }

    while (inicio < fin) {
        int actual = cola[inicio++];
        procesadas++;

        for (int j = 0; j < actividades[actual].cantidad_dependientes; j++) {
            int siguiente = actividades[actual].dependientes[j];
            pendientes[siguiente]--;
            if (pendientes[siguiente] == 0)
                cola[fin++] = siguiente;
        }
    }

    free(pendientes);
    free(cola);
    return procesadas != cantidad;
}

static void ejecutar_hijo(Actividad *a, int fd) {
    struct timespec espera;
    espera.tv_sec = a->tiempo / 1000;
    espera.tv_nsec = (a->tiempo % 1000) * 1000000L;

    while (nanosleep(&espera, &espera) == -1 && errno == EINTR)
        ;

    char mensaje[MAX_MENSAJE];
    snprintf(mensaje, sizeof(mensaje), "OK:%s", a->id);
    write(fd, mensaje, strlen(mensaje) + 1);
    close(fd);
    _exit(0);
}

static int iniciar_actividad(int idx, Proceso *procesos, int max_procesos, int *ejecutando) {
    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        return -1;
    }

    if (pid == 0) {
        close(fd[0]);
        ejecutar_hijo(&actividades[idx], fd[1]);
    }

    close(fd[1]);

    for (int i = 0; i < max_procesos; i++) {
        if (procesos[i].actividad == -1) {
            procesos[i].actividad = idx;
            procesos[i].pid = pid;
            procesos[i].pipe_lectura = fd[0];
            break;
        }
    }

    actividades[idx].pid = pid;
    actividades[idx].estado = CORRIENDO;
    (*ejecutando)++;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return 1;
    }

    char *fin;
    long k_largo = strtol(argv[2], &fin, 10);
    if (*fin != '\0' || k_largo <= 0) {
        fprintf(stderr, "K debe ser un entero positivo.\n");
        return 1;
    }

    int K = (int)k_largo;
    srand((unsigned)time(NULL));

    if (cargar_plan(argv[1]) != 0) {
        fprintf(stderr, "No se pudo cargar el plan.\n");
        return 1;
    }

    if (hay_ciclo()) {
        fprintf(stderr, "El plan contiene un ciclo.\n");
        return 1;
    }

    int *cola = malloc(cantidad * sizeof(int));
    Proceso *procesos = malloc(K * sizeof(Proceso));

    if (cola == NULL || procesos == NULL) {
        fprintf(stderr, "No hay memoria suficiente.\n");
        return 1;
    }

    for (int i = 0; i < K; i++)
        procesos[i].actividad = -1;

    int primero = 0, ultimo = 0;
    for (int i = 0; i < cantidad; i++) {
        if (actividades[i].pendientes == 0) {
            actividades[i].estado = LISTA;
            cola[ultimo++] = i;
        }
    }

    int ejecutando = 0;
    int terminadas = 0;

    printf("Planificador iniciado con paso de mensajes por pipes (K = %d)...\n", K);

    while (terminadas < cantidad) {
        while (ejecutando < K && primero < ultimo) {
            int idx = cola[primero++];
            iniciar_actividad(idx, procesos, K, &ejecutando);
        }

        int status;
        pid_t terminado_pid = wait(&status);
        if (terminado_pid <= 0)
            break;

        for (int i = 0; i < K; i++) {
            if (procesos[i].pid == terminado_pid) {
                char mensaje[MAX_MENSAJE] = {0};
                read(procesos[i].pipe_lectura, mensaje, sizeof(mensaje) - 1);
                close(procesos[i].pipe_lectura);

                int idx = procesos[i].actividad;
                actividades[idx].estado = TERMINADA;
                terminadas++;
                ejecutando--;
                procesos[i].actividad = -1;

                printf("Actividad [%s] termino y envio insumo: %s\n", actividades[idx].nombre, mensaje);

                for (int j = 0; j < actividades[idx].cantidad_dependientes; j++) {
                    int siguiente = actividades[idx].dependientes[j];
                    if (actividades[siguiente].estado == PENDIENTE) {
                        actividades[siguiente].pendientes--;
                        if (actividades[siguiente].pendientes == 0) {
                            actividades[siguiente].estado = LISTA;
                            cola[ultimo++] = siguiente;
                        }
                    }
                }
                break;
            }
        }
    }

    printf("Planificacion completada exitosamente.\n");

    for (int i = 0; i < cantidad; i++) {
        free(actividades[i].dependencias);
        free(actividades[i].dependientes);
    }

    free(actividades);
    free(cola);
    free(procesos);

    return 0;
}
