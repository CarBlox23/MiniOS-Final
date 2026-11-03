#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include "kernel.h"
#include "proc.h"
#include "seguridad.h"

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
        perror("Error al ejecutar BSDGame (verifique que esté instalado)");
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
    if (slot >= total_proc) total_proc = slot + 1;
    esperar_hijo(p);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGINT,  SIG_DFL);
}

void proc_listar(void) {
    printf("\nID | PID    | Usuario          | Nombre          | Estado      | Inicio\n");
    printf("---+--------+------------------+-----------------+-------------+----------------------\n");
    int count = 0;
    for (int i = 0; i < total_proc; i++) {
        if (tabla_proc[i].activo) {
            const char *est_str = tabla_proc[i].estado == EJECUTANDO ? "EJECUTANDO" :
                                  (tabla_proc[i].estado == SUSPENDIDO ? "SUSPENDIDO" : "TERMINADO");
            char t_str[32];
            struct tm *tm_info = localtime(&tabla_proc[i].inicio);
            strftime(t_str, sizeof(t_str), "%H:%M:%S", tm_info);
            printf("%-2d | %-6d | %-16s | %-15s | %-11s | %s\n",
                   i, tabla_proc[i].pid, tabla_proc[i].usuario, tabla_proc[i].nombre, est_str, t_str);
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
    printf("Proceso PID %d finalizado limpiamente (sin zombis).\n", p->pid);
}

void proc_menu(void) {
    while (1) {
        printf("\n--- MiniOS v3.0 | GESTIÓN DE PROCESOS ---\n");
        printf(" 1. Lanzar Tetris (tetris-bsd)\n");
        printf(" 2. Lanzar Snake BSD (snake)\n");
        printf(" 3. Listar Procesos\n");
        printf(" 4. Reanudar Proceso Suspendido (SIGCONT)\n");
        printf(" 5. Terminar Proceso (SIGTERM)\n");
        printf(" 0. Volver al Menú Principal\n");
        switch (leer_entero(" Opción procesos: ")) {
            case 1:
                if (seg_verificar(OBJ_PROCESOS, D_EXEC)) proc_lanzar("tetris-bsd");
                break;
            case 2:
                if (seg_verificar(OBJ_PROCESOS, D_EXEC)) proc_lanzar("snake");
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
            case 0:
                return;
            default:
                if (feof(stdin)) return;
                printf("Opción inválida.\n");
        }
    }
}
