#ifndef PROC_H
#define PROC_H

#include <sys/types.h>
#include <time.h>

#define MAX_PROC 32

typedef enum { EJECUTANDO, SUSPENDIDO, TERMINADO } Estado;

typedef struct {
    pid_t  pid;
    char   nombre[32];
    char   usuario[32];
    Estado estado;
    time_t inicio;
    int    activo;
} PCB;

void proc_menu(void);
void proc_lanzar(const char *programa);
void proc_listar(void);
void proc_reanudar_id(int id);
void proc_terminar_id(int id);

#endif
