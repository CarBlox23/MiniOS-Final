#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <time.h>
#include "kernel.h"
#include "proc.h"
#include "seguridad.h"
#include "bitacora.h"

static PCB tabla_proc[MAX_PROC];
static int total_proc = 0;

static void esperar_hijo(PCB *p) {
    int st = 0;
    waitpid(p->pid, &st, WUNTRACED);
    if (WIFSTOPPED(st)) {
        p->estado = SUSPENDIDO;
        printf("\n[MiniOS] Proceso %s (PID %d) SUSPENDIDO con Ctrl+Z.\n", p->nombre, p->pid);
    } else {
        p->estado = TERMINADO;
        printf("\n[MiniOS] Proceso %s (PID %d) TERMINADO.\n", p->nombre, p->pid);
    }
}

void proc_lanzar(const char *programa) {
    int slot = -1;
    for (int i = 0; i < MAX_PROC; i++) {
        if (!tabla_proc[i].activo || tabla_proc[i].estado == TERMINADO) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        printf("Error: Tabla de procesos llena.\n");
        return;
    }

    signal(SIGTSTP, SIG_IGN);
    signal(SIGINT,  SIG_IGN);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        signal(SIGTSTP, SIG_DFL);
        signal(SIGINT,  SIG_DFL);
        return;
    }
    if (pid == 0) {
        signal(SIGTSTP, SIG_DFL);
        signal(SIGINT,  SIG_DFL);
        execlp(programa, programa, (char *)NULL);
        perror("Error al ejecutar BSDGame (verifique que esté instalado bsdgames)");
        _exit(127);
    }

    PCB *p = &tabla_proc[slot];
    p->pid = pid;
    p->activo = 1;
    strncpy(p->nombre, programa, sizeof(p->nombre) - 1);
    p->nombre[sizeof(p->nombre) - 1] = '\0';
    strncpy(p->usuario, seg_usuario_actual(), sizeof(p->usuario) - 1);
    p->usuario[sizeof(p->usuario) - 1] = '\0';
    p->estado = EJECUTANDO;
    p->inicio = time(NULL);
    p->prioridad = getpriority(PRIO_PROCESS, pid);

    if (slot >= total_proc) total_proc = slot + 1;

    esperar_hijo(p);

    signal(SIGTSTP, SIG_DFL);
    signal(SIGINT,  SIG_DFL);
}

void proc_listar(void) {
    printf("\nID | PID    | Usuario          | Nombre          | Estado      | Nice | Inicio\n");
    printf("---+--------+------------------+-----------------+-------------+------+----------------------\n");
    int count = 0;
    for (int i = 0; i < total_proc; i++) {
        if (tabla_proc[i].activo) {
            const char *est_str = tabla_proc[i].estado == EJECUTANDO ? "EJECUTANDO" :
                                  (tabla_proc[i].estado == SUSPENDIDO ? "SUSPENDIDO" : "TERMINADO");
            char t_str[32];
            struct tm *tm_info = localtime(&tabla_proc[i].inicio);
            strftime(t_str, sizeof(t_str), "%H:%M:%S", tm_info);

            /* Actualizar prioridad actual */
            if (tabla_proc[i].estado != TERMINADO) {
                tabla_proc[i].prioridad = getpriority(PRIO_PROCESS, tabla_proc[i].pid);
            }

            printf("%-2d | %-6d | %-16s | %-15s | %-11s | %-4d | %s\n",
                   i, tabla_proc[i].pid, tabla_proc[i].usuario, tabla_proc[i].nombre,
                   est_str, tabla_proc[i].prioridad, t_str);
            count++;
        }
    }
    if (count == 0) printf("(No hay procesos registrados en la tabla)\n");
}

void proc_reanudar_id(int id) {
    if (id < 0 || id >= total_proc || !tabla_proc[id].activo) {
        printf("ID de proceso inexistente.\n");
        return;
    }
    PCB *p = &tabla_proc[id];
    if (p->estado != SUSPENDIDO) {
        printf("El proceso no está suspendido.\n");
        return;
    }
    if (!seg_es_propietario(p->usuario)) {
        printf("  ACCESO DENEGADO: No puedes reanudar procesos de otro usuario.\n");
        return;
    }

    signal(SIGTSTP, SIG_IGN);
    signal(SIGINT,  SIG_IGN);

    p->estado = EJECUTANDO;
    kill(p->pid, SIGCONT);
    esperar_hijo(p);

    signal(SIGTSTP, SIG_DFL);
    signal(SIGINT,  SIG_DFL);
}

void proc_terminar_id(int id) {
    if (id < 0 || id >= total_proc || !tabla_proc[id].activo) {
        printf("ID de proceso inexistente.\n");
        return;
    }
    PCB *p = &tabla_proc[id];
    if (p->estado == TERMINADO) {
        printf("El proceso ya ha terminado.\n");
        return;
    }
    if (!seg_es_propietario(p->usuario)) {
        printf("  ACCESO DENEGADO: No puedes terminar procesos de otro usuario.\n");
        return;
    }

    kill(p->pid, SIGTERM);
    kill(p->pid, SIGCONT);
    int st = 0;
    waitpid(p->pid, &st, 0);
    p->estado = TERMINADO;
    printf("Proceso PID %d finalizado limpiamente (sin procesos zombis).\n", p->pid);
}

/* Mejora 9.3 (Opción A): Monitor de memoria del proceso */
void proc_memoria(int id) {
    if (id < 0 || id >= total_proc || !tabla_proc[id].activo) {
        printf("ID de proceso inexistente.\n");
        return;
    }
    PCB *p = &tabla_proc[id];
    printf("\n=== MONITOR DE MEMORIA PARA PROCESO '%s' (PID %d) ===\n", p->nombre, p->pid);
    if (p->estado == TERMINADO) {
        printf("  El proceso ya ha terminado. No tiene memoria asignada en el Kernel.\n");
        return;
    }

    char ruta[64], linea[128];
    snprintf(ruta, sizeof ruta, "/proc/%d/status", p->pid);
    FILE *f = fopen(ruta, "r");
    if (!f) {
        printf("  El proceso %d ya no existe en /proc.\n", p->pid);
        return;
    }

    long vmsize_kb = 0, vmrss_kb = 0;
    while (fgets(linea, sizeof linea, f)) {
        if (strncmp(linea, "VmSize:", 7) == 0) {
            printf("  Memoria Virtual (VmSize) : %s", linea + 7);
            sscanf(linea + 7, "%ld", &vmsize_kb);
        } else if (strncmp(linea, "VmRSS:", 6) == 0) {
            printf("  Memoria Física  (VmRSS)  : %s", linea + 6);
            sscanf(linea + 6, "%ld", &vmrss_kb);
        }
    }
    fclose(f);

    long tam_pagina = sysconf(_SC_PAGESIZE);
    if (tam_pagina <= 0) tam_pagina = 4096;
    long paginas_rss = (vmrss_kb * 1024) / tam_pagina;
    long paginas_vmsize = (vmsize_kb * 1024) / tam_pagina;

    printf("  Tamaño de página del SO  : %ld bytes (%ld KB)\n", tam_pagina, tam_pagina / 1024);
    printf("  Páginas físicas ocupadas : %ld páginas en RAM\n", paginas_rss);
    printf("  Páginas virtuales totales: %ld páginas\n", paginas_vmsize);
    printf("===============================================================\n");
}

/* Mejora 9.3 (Opción B): Modificación de prioridades de planificación */
void proc_cambiar_prioridad(int id, int nice_valor) {
    if (id < 0 || id >= total_proc || !tabla_proc[id].activo) {
        printf("ID de proceso inexistente.\n");
        return;
    }
    PCB *p = &tabla_proc[id];
    if (p->estado == TERMINADO) {
        printf("El proceso ya ha terminado.\n");
        return;
    }
    if (!seg_es_propietario(p->usuario)) {
        printf("  ACCESO DENEGADO: No puedes modificar la prioridad de procesos de otro usuario.\n");
        return;
    }

    if (setpriority(PRIO_PROCESS, (id_t)p->pid, nice_valor) != 0) {
        perror("setpriority");
        printf("Nota: Asignar valores de nice negativos (<0) requiere privilegios de superusuario (root).\n");
    } else {
        p->prioridad = getpriority(PRIO_PROCESS, (id_t)p->pid);
        printf("  PID %d (%s) ahora tiene nice %d.\n", p->pid, p->nombre, p->prioridad);
        char resultado[32];
        snprintf(resultado, sizeof resultado, "NICE_%d", p->prioridad);
        auditar("CAMBIO_PRIO", p->nombre, resultado);
    }
}

void proc_menu(void) {
    while (1) {
        printf("\n--- MiniOS v3.0 | GESTIÓN DE PROCESOS ---\n");
        printf(" 1. Lanzar Tetris (tetris-bsd)\n");
        printf(" 2. Lanzar Snake BSD (snake)\n");
        printf(" 3. Listar Procesos (con Nice y Memoria)\n");
        printf(" 4. Reanudar Proceso Suspendido (SIGCONT)\n");
        printf(" 5. Terminar Proceso (SIGTERM)\n");
        printf(" 6. Ver Monitor de Memoria (VmSize, VmRSS y Páginas)\n");
        printf(" 7. Cambiar Prioridad (nice / Planificación)\n");
        printf(" 0. Volver al Menú Principal\n");

        switch (leer_entero(" Opción procesos: ")) {
            case 1:
                if (seg_verificar(OBJ_JUEGOS, D_EXEC)) proc_lanzar("tetris-bsd");
                break;
            case 2:
                if (seg_verificar(OBJ_JUEGOS, D_EXEC)) proc_lanzar("snake");
                break;
            case 3:
                if (seg_verificar(OBJ_PROCESOS, D_READ)) proc_listar();
                break;
            case 4:
                if (seg_verificar(OBJ_PROCESOS, D_UPDATE)) {
                    int id = leer_entero("ID en tabla de proceso a reanudar: ");
                    proc_reanudar_id(id);
                }
                break;
            case 5:
                if (seg_verificar(OBJ_PROCESOS, D_DELETE)) {
                    int id = leer_entero("ID en tabla de proceso a terminar: ");
                    proc_terminar_id(id);
                }
                break;
            case 6:
                if (seg_verificar(OBJ_PROCESOS, D_READ)) {
                    int id = leer_entero("ID en tabla de proceso a inspeccionar memoria: ");
                    proc_memoria(id);
                }
                break;
            case 7:
                if (seg_verificar(OBJ_PROCESOS, D_UPDATE)) {
                    int id = leer_entero("ID en tabla de proceso: ");
                    int nv = leer_entero("Nuevo valor de nice (-20 a 19): ");
                    proc_cambiar_prioridad(id, nv);
                }
                break;
            case 0:
                return;
            default:
                if (feof(stdin)) return;
                printf("Opción inválida.\n");
        }
    }
}
