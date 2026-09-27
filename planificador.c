#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return 1;
    }

    if (cargar_plan(argv[1]) != 0) {
        fprintf(stderr, "No se pudo cargar el plan.\n");
        return 1;
    }

    printf("Plan cargado correctamente. Total actividades: %d\n", cantidad);

    for (int i = 0; i < cantidad; i++) {
        free(actividades[i].dependencias);
        free(actividades[i].dependientes);
    }
    free(actividades);

    return 0;
}
