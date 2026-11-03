#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kernel.h"
#include "fs.h"
#include "seguridad.h"

#define MAGIC   0x4D494E49
#define MAX_REG 100

typedef struct {
    unsigned int magic, version, total, usados, siguiente_id, limpio;
} Superbloque;

typedef struct {
    int  id, activo;
    char nombre[50], descripcion[100], propietario[32];
} Registro;

static int disco_fd = -1;
static Superbloque sb;

static off_t pos_registro(int i) {
    return (off_t)sizeof(Superbloque) + (off_t)i * sizeof(Registro);
}

static int leer_registro(int i, Registro *r) {
    if (lseek(disco_fd, pos_registro(i), SEEK_SET) == (off_t)-1) return -1;
    return read(disco_fd, r, sizeof *r) == (ssize_t)sizeof *r ? 0 : -1;
}

static int escribir_registro(int i, const Registro *r) {
    if (lseek(disco_fd, pos_registro(i), SEEK_SET) == (off_t)-1) return -1;
    return write(disco_fd, r, sizeof *r) == (ssize_t)sizeof *r ? 0 : -1;
}

static void guardar_superbloque(void) {
    if (disco_fd < 0 || lseek(disco_fd, 0, SEEK_SET) == (off_t)-1) return;
    ssize_t n = write(disco_fd, &sb, sizeof sb);
    (void)n;
    fsync(disco_fd);
}

int fs_montar(const char *ruta) {
    mkdir("datos", 0755);
    disco_fd = open(ruta, O_RDWR | O_CREAT, 0600);
    if (disco_fd < 0) {
        perror("fs_montar: open");
        return -1;
    }
    ssize_t leidos = read(disco_fd, &sb, sizeof sb);
    if (leidos != (ssize_t)sizeof sb || sb.magic != MAGIC) {
        printf("Disco sin formato o corrupto. Formateando disco virtual...\n");
        memset(&sb, 0, sizeof sb);
        sb.magic = MAGIC;
        sb.version = 3;
        sb.total = MAX_REG;
        sb.usados = 0;
        sb.siguiente_id = 1;
        sb.limpio = 1;
        Registro vacio;
        memset(&vacio, 0, sizeof vacio);
        for (int i = 0; i < MAX_REG; i++) escribir_registro(i, &vacio);
    }
    if (!sb.limpio) {
        printf("⚠️  ADVERTENCIA: El disco no se desmontó correctamente en la última sesión.\n");
    }
    sb.limpio = 0;
    guardar_superbloque();
    return 0;
}

void fs_desmontar(void) {
    if (disco_fd >= 0) {
        sb.limpio = 1;
        guardar_superbloque();
        close(disco_fd);
        disco_fd = -1;
    }
}

void fs_crear(void) {
    if (sb.usados >= sb.total) {
        printf("Error: Disco virtual lleno.\n");
        return;
    }
    int slot = -1;
    Registro r;
    for (int i = 0; i < MAX_REG; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 0) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        printf("Error: No hay ranuras libres.\n");
        return;
    }
    memset(&r, 0, sizeof r);
    r.id = sb.siguiente_id++;
    r.activo = 1;
    strncpy(r.propietario, seg_usuario_actual(), sizeof(r.propietario) - 1);
    leer_linea("Nombre del registro     : ", r.nombre, sizeof r.nombre);
    leer_linea("Descripción del registro: ", r.descripcion, sizeof r.descripcion);
    if (escribir_registro(slot, &r) == 0) {
        sb.usados++;
        guardar_superbloque();
        printf("Registro #%d creado en la ranura %d por '%s'.\n", r.id, slot, r.propietario);
    } else {
        printf("Error al escribir el registro en disco.\n");
    }
}

void fs_listar(void) {
    printf("\nID  | Ranura | Propietario      | Nombre               | Descripción\n");
    printf("----+--------+------------------+----------------------+--------------------------\n");
    Registro r;
    int encontrados = 0;
    for (int i = 0; i < MAX_REG; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1) {
            printf("%-3d | %-6d | %-16s | %-20s | %s\n", r.id, i, r.propietario, r.nombre, r.descripcion);
            encontrados++;
        }
    }
    if (encontrados == 0) printf("(No hay registros activos en el disco virtual)\n");
    printf("Total: %d registros activos.\n", encontrados);
}

void fs_actualizar(void) {
    int id = leer_entero("ID del registro a actualizar: ");
    if (id <= 0) { printf("ID inválido.\n"); return; }
    Registro r;
    int slot = -1;
    for (int i = 0; i < MAX_REG; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1 && r.id == id) {
            slot = i;
            break;
        }
    }
    if (slot == -1) { printf("Registro no encontrado.\n"); return; }
    if (!seg_es_propietario(r.propietario)) {
        printf("  ACCESO DENEGADO: No eres el propietario del registro.\n");
        return;
    }
    printf("Actualizando '%s' (Creador: %s)\n", r.nombre, r.propietario);
    leer_linea("Nuevo nombre (Enter mantiene actual): ", r.nombre, sizeof r.nombre);
    leer_linea("Nueva descripción                   : ", r.descripcion, sizeof r.descripcion);
    if (escribir_registro(slot, &r) == 0) {
        guardar_superbloque();
        printf("Registro actualizado con éxito.\n");
    }
}

void fs_borrar(void) {
    int id = leer_entero("ID del registro a borrar: ");
    if (id <= 0) { printf("ID inválido.\n"); return; }
    Registro r;
    int slot = -1;
    for (int i = 0; i < MAX_REG; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1 && r.id == id) {
            slot = i;
            break;
        }
    }
    if (slot == -1) { printf("Registro no encontrado.\n"); return; }
    if (!seg_es_propietario(r.propietario)) {
        printf("  ACCESO DENEGADO: No tienes permiso para borrar este registro.\n");
        return;
    }
    r.activo = 0;
    if (escribir_registro(slot, &r) == 0) {
        if (sb.usados > 0) sb.usados--;
        guardar_superbloque();
        printf("Registro #%d eliminado (Ranura %d disponible para reciclaje).\n", id, slot);
    }
}

void fs_estado(void) {
    printf("\n===== ESTADO DEL DISCO VIRTUAL =====\n");
    printf("Magic        : 0x%X\n", sb.magic);
    printf("Versión      : %u\n", sb.version);
    printf("Capacidad    : %u ranuras\n", sb.total);
    printf("Usados       : %u ranuras\n", sb.usados);
    printf("Libres       : %u ranuras\n", sb.total - sb.usados);
    printf("Siguiente ID : %u\n", sb.siguiente_id);
    printf("Estado       : %s\n", sb.limpio ? "Limpio" : "En uso (Desmontaje pendiente)");
    printf("\nMapa de Ocupación (. = Libre, # = Ocupado):\n");
    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1) printf("#");
        else printf(".");
        if ((i + 1) % 25 == 0) printf("\n");
    }
    printf("====================================\n");
}

int fs_registrar_record(const char *jugador, int puntaje) {
    if (sb.usados >= sb.total) return -1;
    int slot = -1;
    Registro r;
    for (int i = 0; i < MAX_REG; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 0) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;
    memset(&r, 0, sizeof r);
    r.id = sb.siguiente_id++;
    r.activo = 1;
    strncpy(r.propietario, jugador, sizeof(r.propietario) - 1);
    snprintf(r.nombre, sizeof(r.nombre), "RECORD_%s_%d", jugador, puntaje);
    snprintf(r.descripcion, sizeof(r.descripcion), "Puntaje obtenido en minijuego Snake: %d pts", puntaje);
    if (escribir_registro(slot, &r) == 0) {
        sb.usados++;
        guardar_superbloque();
        return 0;
    }
    return -1;
}
