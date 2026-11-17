#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kernel.h"
#include "fs.h"
#include "seguridad.h"
#include "bitacora.h"

#define MAGIC       0x4D494E49
#define MAX_REG     100
#define RUTA_DISCO  DIR_DATOS "minios.disk"
#define RUTA_ESPEJO DIR_DATOS "minios_espejo.disk"

typedef struct {
    unsigned int magic, version, total, usados, siguiente_id, limpio;
} Superbloque;

typedef struct {
    int  id, activo;
    char nombre[50], descripcion[100], propietario[32];
} Registro;

static int disco_fd = -1;
static int espejo_fd = -1;
static Superbloque sb;

static off_t pos_registro(int i) {
    return (off_t)sizeof(Superbloque) + (off_t)i * sizeof(Registro);
}

/* Escribe simultáneamente en ambos discos (RAID 1) y fuerza persistencia física */
static int escribir_en_ambos(off_t pos, const void *datos, size_t n) {
    int ok = 0;
    if (disco_fd >= 0 && lseek(disco_fd, pos, SEEK_SET) >= 0 && write(disco_fd, datos, n) == (ssize_t)n) {
        fsync(disco_fd);
        ok++;
    }
    if (espejo_fd >= 0 && lseek(espejo_fd, pos, SEEK_SET) >= 0 && write(espejo_fd, datos, n) == (ssize_t)n) {
        fsync(espejo_fd);
        ok++;
    }
    return (ok > 0) ? 0 : -1;
}

/* Reconstrucción total: copia exacta bloque a bloque del disco sano al dañado */
static int copiar_disco(int origen, int destino) {
    char bloque[4096];
    ssize_t n;
    if (lseek(origen, 0, SEEK_SET) == (off_t)-1) return -1;
    if (lseek(destino, 0, SEEK_SET) == (off_t)-1) return -1;
    while ((n = read(origen, bloque, sizeof bloque)) > 0) {
        if (write(destino, bloque, (size_t)n) != n) return -1;
    }
    fsync(destino);
    return 0;
}

static int leer_registro(int i, Registro *r) {
    if (disco_fd >= 0 && lseek(disco_fd, pos_registro(i), SEEK_SET) != (off_t)-1) {
        if (read(disco_fd, r, sizeof *r) == (ssize_t)sizeof *r) return 0;
    }
    /* Si falla el principal, leer del disco espejo */
    if (espejo_fd >= 0 && lseek(espejo_fd, pos_registro(i), SEEK_SET) != (off_t)-1) {
        if (read(espejo_fd, r, sizeof *r) == (ssize_t)sizeof *r) return 0;
    }
    return -1;
}

static int escribir_registro(int i, const Registro *r) {
    return escribir_en_ambos(pos_registro(i), r, sizeof *r);
}

static void guardar_superbloque(void) {
    escribir_en_ambos(0, &sb, sizeof sb);
}

int fs_montar(const char *ruta) {
    mkdir("datos", 0755);
    const char *ruta_p = (ruta && ruta[0]) ? ruta : RUTA_DISCO;

    disco_fd = open(ruta_p, O_RDWR | O_CREAT, 0600);
    espejo_fd = open(RUTA_ESPEJO, O_RDWR | O_CREAT, 0600);
    chmod(ruta_p, 0600);
    chmod(RUTA_ESPEJO, 0600);

    if (disco_fd < 0 || espejo_fd < 0) {
        perror("fs_montar: open");
        return -1;
    }

    Superbloque sb1, sb2;
    memset(&sb1, 0, sizeof sb1);
    memset(&sb2, 0, sizeof sb2);

    ssize_t leidos1 = read(disco_fd, &sb1, sizeof sb1);
    ssize_t leidos2 = read(espejo_fd, &sb2, sizeof sb2);

    int disco_sano  = (leidos1 == (ssize_t)sizeof sb1 && sb1.magic == MAGIC);
    int espejo_sano = (leidos2 == (ssize_t)sizeof sb2 && sb2.magic == MAGIC);

    if (!disco_sano && !espejo_sano) {
        printf("[RAID 1] Discos sin formato o dañados. Formateando arreglo de almacenamiento...\n");
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
    } else if (disco_sano && !espejo_sano) {
        printf("⚠️  [RAID 1] Espejo dañado o ausente. Reconstruyendo espejo desde disco principal...\n");
        copiar_disco(disco_fd, espejo_fd);
        sb = sb1;
        auditar("RAID1_RECONSTR", "espejo", "OK");
        printf("✅  [RAID 1] Espejo reconstruido exitosamente desde el disco principal.\n");
    } else if (!disco_sano && espejo_sano) {
        printf("⚠️  [RAID 1] Disco principal dañado o ausente. Reconstruyendo desde el espejo...\n");
        copiar_disco(espejo_fd, disco_fd);
        sb = sb2;
        auditar("RAID1_RECONSTR", "principal", "OK");
        printf("✅  [RAID 1] Disco reconstruido desde el espejo.\n");
    } else {
        sb = sb1;
    }

    if (!sb.limpio) {
        printf("⚠️  ADVERTENCIA: El disco no se desmontó correctamente en la última sesión (apagado sucio detectado).\n");
    }

    sb.limpio = 0;
    guardar_superbloque();
    return 0;
}

void fs_desmontar(void) {
    if (disco_fd >= 0 || espejo_fd >= 0) {
        sb.limpio = 1;
        guardar_superbloque();
        if (disco_fd >= 0)  { close(disco_fd);  disco_fd = -1; }
        if (espejo_fd >= 0) { close(espejo_fd); espejo_fd = -1; }
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
        printf("Registro #%d creado en la ranura %d por '%s' (sincronizado en RAID 1).\n",
               r.id, slot, r.propietario);
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
        printf("Registro actualizado con éxito en ambos discos del arreglo RAID 1.\n");
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
        printf("Registro #%d eliminado (Ranura %d disponible para reciclaje en RAID 1).\n", id, slot);
    }
}

void fs_estado(void) {
    printf("\n===== ESTADO DEL DISCO VIRTUAL (RAID 1) =====\n");
    printf("Arreglo      : RAID 1 Espejo Activo (minios.disk <-> minios_espejo.disk)\n");
    printf("Magic        : 0x%X\n", sb.magic);
    printf("Versión      : %u\n", sb.version);
    printf("Capacidad    : %u ranuras\n", sb.total);
    printf("Usados       : %u ranuras\n", sb.usados);
    printf("Libres       : %u ranuras\n", sb.total - sb.usados);
    printf("Siguiente ID : %u\n", sb.siguiente_id);
    printf("Estado       : %s\n", sb.limpio ? "Limpio (Desmontado ordenadamente)" : "En uso (Desmontaje pendiente)");
    printf("\nMapa de Ocupación (. = Libre, # = Ocupado):\n");
    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1) printf("#");
        else printf(".");
        if ((i + 1) % 25 == 0) printf("\n");
    }
    printf("=============================================\n");
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
