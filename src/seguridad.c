#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kernel.h"
#include "seguridad.h"

#define ARCH_MATRIZ DIR_DATOS "matriz.dat"

static unsigned char matriz[NUM_ROLES][NUM_OBJETOS] = {
    /* ROL_ADMIN    */ { D_CREATE|D_READ|D_UPDATE|D_DELETE, D_EXEC, D_READ,
                         D_READ|D_UPDATE|D_DELETE|D_EXEC, D_READ|D_UPDATE, D_EXEC },
    /* ROL_OPERADOR */ { D_CREATE|D_READ|D_UPDATE,          D_EXEC, D_CREATE|D_READ|D_UPDATE,
                         D_READ|D_UPDATE|D_DELETE|D_EXEC, D_READ,          0      },
    /* ROL_INVITADO */ { D_READ,                            D_EXEC, 0,
                         D_READ,                          0,               0      }
};

static const char *NOMBRE_ROL[NUM_ROLES] = { "admin", "operador", "invitado" };
static const char *NOMBRE_OBJ[NUM_OBJETOS] = { "registros", "juegos", "partidas", "procesos", "matriz", "sistema" };

static int rol_actual = -1;

void seg_iniciar_sesion(void) {
    int r;
    do {
        printf("\nIniciar Sesión en MiniOS:\n");
        r = leer_entero("Seleccione Rol (0=admin, 1=operador, 2=invitado): ");
        if (feof(stdin)) exit(0);
    } while (r < 0 || r >= NUM_ROLES);
    rol_actual = r;
    printf("Sesión iniciada correctamente como: [%s]\n", NOMBRE_ROL[rol_actual]);
}

const char *seg_usuario_actual(void) {
    return rol_actual < 0 ? "nadie" : NOMBRE_ROL[rol_actual];
}

Rol seg_rol_actual(void) {
    return (Rol)rol_actual;
}

int seg_verificar(Objeto obj, unsigned char derecho) {
    if (rol_actual < 0) return 0;
    if ((matriz[rol_actual][obj] & derecho) == derecho) return 1;
    printf("  ACCESO DENEGADO (Derechos insuficientes en matriz de acceso)\n");
    return 0;
}

int seg_es_propietario(const char *owner) {
    if (rol_actual == ROL_ADMIN) return 1;
    if (strncmp(owner, seg_usuario_actual(), 32) == 0) return 1;
    return 0;
}

void seg_guardar_matriz(void) {
    mkdir("datos", 0755);
    int fd = open(ARCH_MATRIZ, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { perror("seg_guardar_matriz: open"); return; }
    ssize_t n = write(fd, matriz, sizeof matriz);
    (void)n;
    fsync(fd);
    close(fd);
}

void seg_cargar_matriz(void) {
    int fd = open(ARCH_MATRIZ, O_RDONLY);
    if (fd < 0) {
        seg_guardar_matriz();
        return;
    }
    if (read(fd, matriz, sizeof matriz) != (ssize_t)sizeof matriz) {
        printf("matriz.dat dañada: restaurando configuración por defecto.\n");
        seg_guardar_matriz();
    }
    close(fd);
}

static void derechos_a_cadena(unsigned char d, char *str) {
    str[0] = (d & D_CREATE) ? 'C' : '-';
    str[1] = (d & D_READ)   ? 'R' : '-';
    str[2] = (d & D_UPDATE) ? 'U' : '-';
    str[3] = (d & D_DELETE) ? 'D' : '-';
    str[4] = (d & D_EXEC)   ? 'X' : '-';
    str[5] = '\0';
}

void seg_ver_matriz(void) {
    printf("\n========== MATRIZ DE CONTROL DE ACCESO (PROTECCIÓN) ==========\n");
    printf("%-10s | %-8s | %-8s | %-8s | %-8s | %-8s | %-8s\n",
           "Dominio", "Regist", "Juegos", "Partid", "Proces", "Matriz", "Sistem");
    printf("-----------+----------+----------+----------+----------+----------+---------\n");
    for (int r = 0; r < NUM_ROLES; r++) {
        char c[NUM_OBJETOS][8];
        for (int o = 0; o < NUM_OBJETOS; o++) {
            derechos_a_cadena(matriz[r][o], c[o]);
        }
        printf("%-10s | %-8s | %-8s | %-8s | %-8s | %-8s | %-8s\n",
               NOMBRE_ROL[r], c[0], c[1], c[2], c[3], c[4], c[5]);
    }
    printf("Leyenda: C=Create, R=Read, U=Update, D=Delete, X=Exec, -=Denegado\n");
    printf("==============================================================\n");
}

void seg_modificar_matriz(void) {
    if (rol_actual != ROL_ADMIN) {
        printf("  ACCESO DENEGADO: Solo el administrador puede modificar la matriz.\n");
        return;
    }
    printf("\n--- MODIFICACIÓN DE LA MATRIZ DE ACCESO ---\n");
    int r = leer_entero("Rol a modificar (0=admin, 1=operador, 2=invitado): ");
    if (r < 0 || r >= NUM_ROLES) { printf("Rol inválido.\n"); return; }
    int o = leer_entero("Objeto (0=reg, 1=juego, 2=partida, 3=proc, 4=matriz, 5=sistema): ");
    if (o < 0 || o >= NUM_OBJETOS) { printf("Objeto inválido.\n"); return; }
    printf("Introduzca nuevos derechos en bits (Suma: C=1, R=2, U=4, D=8, X=16 | 0=Ninguno):\n");
    int bits = leer_entero("Valor decimal de derechos: ");
    if (bits < 0 || bits > 31) { printf("Valor de derechos inválido.\n"); return; }
    // Blindaje de seguridad: el admin no puede quitarse control de la matriz
    if (r == ROL_ADMIN && o == OBJ_MATRIZ && !(bits & D_UPDATE)) {
        printf("Error de Seguridad: El Administrador no puede removerse a sí mismo el control de la matriz.\n");
        return;
    }
    matriz[r][o] = (unsigned char)bits;
    seg_guardar_matriz();
    printf("Matriz actualizada exitosamente y persistida en disco.\n");
}
