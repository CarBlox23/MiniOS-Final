#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kernel.h"
#include "seguridad.h"
#include "bitacora.h"

#define ARCH_BITACORA DIR_DATOS "bitacora.log"
#define MAX_TRACK_USERS 128

typedef struct {
    char usuario[32];
    int  conteo;
} ConteoUsuario;

void auditar(const char *accion, const char *objeto, const char *resultado) {
    char fecha[32], linea[256];
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    if (!tm_info) return;

    strftime(fecha, sizeof fecha, "%Y-%m-%d %H:%M:%S", tm_info);
    int n = snprintf(linea, sizeof linea, "%s | %-10s | %-8s | %-14s | %-10s | %s\n",
                     fecha, seg_usuario_actual(), seg_rol_actual(), accion, objeto, resultado);

    mkdir("datos", 0755);
    int fd = open(ARCH_BITACORA, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    chmod(ARCH_BITACORA, 0600);
    ssize_t escritos = write(fd, linea, (size_t)n);
    (void)escritos;
    fsync(fd);
    close(fd);
}

void bitacora_mostrar(void) {
    FILE *f = fopen(ARCH_BITACORA, "r");
    if (!f) {
        printf("\nBitácora vacía o no encontrada (%s).\n", ARCH_BITACORA);
        return;
    }

    printf("\n=============================== BITÁCORA DE AUDITORÍA ===============================\n");
    printf("Fecha y Hora        | Usuario    | Rol      | Acción         | Objeto     | Resultado\n");
    printf("--------------------+------------+----------+----------------+------------+------------------\n");

    char linea[256];
    int denegados = 0, fallidos = 0;
    ConteoUsuario conteos[MAX_TRACK_USERS];
    int total_track = 0;
    memset(conteos, 0, sizeof conteos);

    while (fgets(linea, sizeof linea, f)) {
        printf("%s", linea);

        if (strstr(linea, "DENEGADO")) {
            denegados++;

            /* Extraer nombre de usuario de la columna 2: "fecha | usuario | ..." */
            char *p1 = strchr(linea, '|');
            if (p1) {
                p1++;
                while (*p1 == ' ') p1++;
                char u_temp[32] = {0};
                int idx = 0;
                while (*p1 && *p1 != ' ' && *p1 != '|' && idx < (int)sizeof(u_temp) - 1) {
                    u_temp[idx++] = *p1++;
                }
                u_temp[idx] = '\0';

                if (u_temp[0] != '\0' && strcmp(u_temp, "-") != 0) {
                    int hallado = 0;
                    for (int i = 0; i < total_track; i++) {
                        if (strcmp(conteos[i].usuario, u_temp) == 0) {
                            conteos[i].conteo++;
                            hallado = 1;
                            break;
                        }
                    }
                    if (!hallado && total_track < MAX_TRACK_USERS) {
                        strncpy(conteos[total_track].usuario, u_temp, sizeof(conteos[total_track].usuario) - 1);
                        conteos[total_track].conteo = 1;
                        total_track++;
                    }
                }
            }
        }

        if (strstr(linea, "LOGIN FALLIDO")) {
            fallidos++;
        }
    }
    fclose(f);

    /* Determinar usuario con más denegaciones */
    char usuario_max[32] = "Ninguno";
    int max_deneg = 0;
    for (int i = 0; i < total_track; i++) {
        if (conteos[i].conteo > max_deneg) {
            max_deneg = conteos[i].conteo;
            strncpy(usuario_max, conteos[i].usuario, sizeof(usuario_max) - 1);
        }
    }

    printf("\n============================= RESUMEN DE AUDITORÍA =============================\n");
    printf("Total de accesos denegados   : %d\n", denegados);
    printf("Total de logins fallidos     : %d\n", fallidos);
    if (max_deneg > 0) {
        printf("Usuario con más denegaciones : %s (%d denegaciones registradas)\n", usuario_max, max_deneg);
    } else {
        printf("Usuario con más denegaciones : Ninguno (0 denegaciones registradas)\n");
    }
    printf("================================================================================\n");
}
