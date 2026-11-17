#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include "kernel.h"
#include "seguridad.h"
#include "bitacora.h"

#define ARCH_USUARIOS DIR_DATOS "usuarios.dat"
#define ARCH_MATRIZ   DIR_DATOS "matriz.dat"
#define MAX_INTENTOS  3

typedef struct {
    char          nombre[32];
    unsigned long sal, hash;                 /* NUNCA la contraseña */
    int           rol, intentos_fallidos, bloqueado;
} Usuario;

static Usuario sesion;
static int     hay_sesion = 0;
static int     denegaciones_sesion = 0;

static unsigned char matriz[NUM_ROLES][NUM_OBJETOS] = {
    /* ROL_ADMIN    */ { D_CREATE|D_READ|D_UPDATE|D_DELETE, D_EXEC, D_CREATE|D_READ|D_UPDATE|D_DELETE,
                         D_READ|D_UPDATE|D_DELETE|D_EXEC, D_READ|D_UPDATE, D_EXEC,
                         D_CREATE|D_READ|D_UPDATE, D_READ, D_EXEC },
    /* ROL_OPERADOR */ { D_CREATE|D_READ|D_UPDATE,          D_EXEC, D_CREATE|D_READ|D_UPDATE,
                         D_READ|D_UPDATE|D_DELETE|D_EXEC, D_READ,          0,
                         0,                        0,      D_EXEC },
    /* ROL_INVITADO */ { D_READ,                            D_EXEC, 0,
                         D_READ,                          0,               0,
                         0,                        0,      0      }
};

static const char *NOMBRE_ROL[NUM_ROLES] = { "admin", "operador", "invitado" };
static const char *NOMBRE_OBJ[NUM_OBJETOS] = {
    "registros", "juegos", "partidas", "procesos", "matriz", "sistema",
    "usuarios", "bitacora", "virtualiz"
};

/* Hash didáctico (djb2 con sal). */
static unsigned long hash_clave(const char *clave, unsigned long sal) {
    unsigned long h = 5381 ^ sal;
    for (const unsigned char *p = (const unsigned char *)clave; *p; p++)
        h = ((h << 5) + h) + *p;
    return h;
}

static void leer_clave(const char *prompt, char *buf, int n) {
    struct termios viejo, nuevo;
    int tty = isatty(STDIN_FILENO);
    if (tty) {
        tcgetattr(STDIN_FILENO, &viejo);
        nuevo = viejo;
        nuevo.c_lflag &= ~ECHO;              /* apagar el eco: la clave no se ve */
        tcsetattr(STDIN_FILENO, TCSANOW, &nuevo);
    }
    leer_linea(prompt, buf, n);
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &viejo);
        printf("\n");
    }
}

static void crear_usuario(const char *nombre, const char *clave, Rol rol) {
    mkdir("datos", 0755);
    Usuario u;
    memset(&u, 0, sizeof u);
    strncpy(u.nombre, nombre, sizeof(u.nombre) - 1);
    u.sal  = ((unsigned long)rand() << 16) ^ (unsigned long)time(NULL) ^ (unsigned long)getpid();
    u.hash = hash_clave(clave, u.sal);
    u.rol  = (int)rol;
    u.intentos_fallidos = 0;
    u.bloqueado = 0;

    int fd = open(ARCH_USUARIOS, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    chmod(ARCH_USUARIOS, 0600);
    ssize_t n = write(fd, &u, sizeof u);
    (void)n;
    fsync(fd);
    close(fd);
}

static int buscar_usuario(const char *nombre, Usuario *u, off_t *pos) {
    int fd = open(ARCH_USUARIOS, O_RDONLY);
    if (fd < 0) return 0;
    off_t p = 0;
    while (read(fd, u, sizeof *u) == (ssize_t)sizeof *u) {
        if (strcmp(u->nombre, nombre) == 0) {
            if (pos) *pos = p;
            close(fd);
            return 1;
        }
        p += sizeof *u;
    }
    close(fd);
    return 0;
}

static void guardar_usuario(const Usuario *u, off_t pos) {
    int fd = open(ARCH_USUARIOS, O_WRONLY);
    if (fd < 0) return;
    lseek(fd, pos, SEEK_SET);
    ssize_t n = write(fd, u, sizeof *u);
    (void)n;
    fsync(fd);
    close(fd);
}

int seg_login(void) {
    if (access(ARCH_USUARIOS, F_OK) != 0) {
        mkdir("datos", 0755);
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        crear_usuario("admin",    "admin123", ROL_ADMIN);
        crear_usuario("operador", "oper123",  ROL_OPERADOR);
        crear_usuario("invitado", "inv123",   ROL_INVITADO);
    }

    char nombre[32], clave[64];
    Usuario u;
    off_t pos = 0;

    printf("\n=========================================================================\n");
    printf("                       INICIAR SESIÓN EN MINIOS\n");
    printf("=========================================================================\n");
    leer_linea(" Usuario: ", nombre, sizeof nombre);
    if (feof(stdin)) exit(0);
    if (nombre[0] == '\0') return 0;

    leer_clave(" Contraseña: ", clave, sizeof clave);
    if (feof(stdin)) exit(0);

    if (!buscar_usuario(nombre, &u, &pos)) {
        auditar("LOGIN", nombre, "LOGIN FALLIDO");
        printf("Usuario o contraseña incorrectos.\n");
        return 0;
    }

    if (u.bloqueado) {
        auditar("LOGIN", nombre, "CUENTA BLOQUEADA");
        printf("Cuenta bloqueada. Contacte al administrador.\n");
        return 0;
    }

    if (hash_clave(clave, u.sal) != u.hash) {
        u.intentos_fallidos++;
        if (u.intentos_fallidos >= MAX_INTENTOS) {
            u.bloqueado = 1;
        }
        guardar_usuario(&u, pos);
        auditar("LOGIN", nombre, u.bloqueado ? "BLOQUEO" : "LOGIN FALLIDO");
        printf("Usuario o contraseña incorrectos.\n");
        return 0;
    }

    /* Login exitoso: reiniciar contador de fallos y cargar sesión */
    u.intentos_fallidos = 0;
    guardar_usuario(&u, pos);
    sesion = u;
    hay_sesion = 1;
    denegaciones_sesion = 0;
    auditar("LOGIN", nombre, "LOGIN EXITOSO");
    printf("\n[MiniOS] Inicio de sesión exitoso. ¡Bienvenido, %s! (Rol: %s)\n",
           sesion.nombre, NOMBRE_ROL[sesion.rol]);
    return 1;
}

void seg_cerrar_sesion(void) {
    if (hay_sesion) {
        auditar("LOGOUT", sesion.nombre, "OK");
        printf("\nSesión finalizada para '%s'. Volviendo al login...\n", sesion.nombre);
    }
    hay_sesion = 0;
    denegaciones_sesion = 0;
    memset(&sesion, 0, sizeof sesion);
}

const char *seg_usuario_actual(void) {
    return hay_sesion ? sesion.nombre : "-";
}

const char *seg_rol_actual(void) {
    return (hay_sesion && sesion.rol >= 0 && sesion.rol < NUM_ROLES) ? NOMBRE_ROL[sesion.rol] : "-";
}

int seg_verificar(Objeto obj, unsigned char derecho) {
    char accion[32];
    if (derecho == D_CREATE) snprintf(accion, sizeof accion, "CREATE");
    else if (derecho == D_READ) snprintf(accion, sizeof accion, "READ");
    else if (derecho == D_UPDATE) snprintf(accion, sizeof accion, "UPDATE");
    else if (derecho == D_DELETE) snprintf(accion, sizeof accion, "DELETE");
    else if (derecho == D_EXEC) snprintf(accion, sizeof accion, "EXEC");
    else snprintf(accion, sizeof accion, "DERECHO_%02X", derecho);

    const char *nombre_obj = (obj >= 0 && obj < NUM_OBJETOS) ? NOMBRE_OBJ[obj] : "desconocido";

    if (hay_sesion && (matriz[sesion.rol][obj] & derecho) == derecho) {
        auditar(accion, nombre_obj, "PERMITIDO");
        return 1;
    }

    auditar(accion, nombre_obj, "DENEGADO");
    printf("  ACCESO DENEGADO: Derechos insuficientes para '%s' sobre objeto '%s'\n",
           seg_usuario_actual(), nombre_obj);

    /* Alerta de 5 denegaciones acumuladas en la sesión */
    denegaciones_sesion++;
    if (denegaciones_sesion == 5) {
        printf("\n⚠️  [ALERTA DE SEGURIDAD]: El usuario '%s' ha acumulado 5 accesos denegados en esta sesión.\n",
               seg_usuario_actual());
        auditar("ALERTA_SEG", seg_usuario_actual(), "5 DENEGACIONES ACUMULADAS");
    }

    return 0;
}

int seg_es_propietario(const char *owner) {
    if (!hay_sesion) return 0;
    if (sesion.rol == ROL_ADMIN) return 1;
    if (strncmp(owner, seg_usuario_actual(), 32) == 0) return 1;
    return 0;
}

void seg_gestionar_usuarios(void) {
    if (!seg_verificar(OBJ_USUARIOS, D_READ)) return;

    while (1) {
        printf("\n--- MiniOS v3.0 | GESTIÓN DE USUARIOS ---\n");
        printf(" 1. Listar Usuarios\n");
        printf(" 2. Crear Nuevo Usuario\n");
        printf(" 3. Desbloquear Usuario\n");
        printf(" 0. Volver al Menú Principal\n");

        switch (leer_entero(" Opción usuarios: ")) {
            case 1: {
                if (!seg_verificar(OBJ_USUARIOS, D_READ)) break;
                int fd = open(ARCH_USUARIOS, O_RDONLY);
                if (fd < 0) { printf("Error al abrir archivo de usuarios.\n"); break; }
                printf("\n%-16s | %-10s | %-16s | %-10s\n", "Nombre", "Rol", "Fallos Seguidos", "Estado");
                printf("-----------------+------------+------------------+-----------\n");
                Usuario u;
                int count = 0;
                while (read(fd, &u, sizeof u) == (ssize_t)sizeof u) {
                    const char *r_str = (u.rol >= 0 && u.rol < NUM_ROLES) ? NOMBRE_ROL[u.rol] : "desconocido";
                    printf("%-16s | %-10s | %-16d | %s\n",
                           u.nombre, r_str, u.intentos_fallidos, u.bloqueado ? "BLOQUEADO" : "ACTIVO");
                    count++;
                }
                close(fd);
                printf("Total: %d usuarios registrados.\n", count);
                break;
            }
            case 2: {
                if (!seg_verificar(OBJ_USUARIOS, D_CREATE)) break;
                char nuevo_nombre[32], nueva_clave[64];
                Usuario u;
                leer_linea("Nombre de nuevo usuario: ", nuevo_nombre, sizeof nuevo_nombre);
                if (nuevo_nombre[0] == '\0') { printf("Nombre no puede estar vacío.\n"); break; }
                if (buscar_usuario(nuevo_nombre, &u, NULL)) {
                    printf("Error: Ya existe un usuario con el nombre '%s'.\n", nuevo_nombre);
                    break;
                }
                printf("Roles disponibles: 0=admin, 1=operador, 2=invitado\n");
                int r = leer_entero("Seleccione rol: ");
                if (r < 0 || r >= NUM_ROLES) { printf("Rol inválido.\n"); break; }
                leer_clave("Contraseña (mínimo 6 caracteres): ", nueva_clave, sizeof nueva_clave);
                if (strlen(nueva_clave) < 6) {
                    printf("Error: La contraseña debe tener al menos 6 caracteres.\n");
                    break;
                }
                crear_usuario(nuevo_nombre, nueva_clave, (Rol)r);
                auditar("CREAR_USUARIO", nuevo_nombre, "OK");
                printf("Usuario '%s' creado exitosamente con rol '%s'.\n", nuevo_nombre, NOMBRE_ROL[r]);
                break;
            }
            case 3: {
                if (!seg_verificar(OBJ_USUARIOS, D_UPDATE)) break;
                char nom_desbloq[32];
                leer_linea("Nombre del usuario a desbloquear: ", nom_desbloq, sizeof nom_desbloq);
                Usuario u;
                off_t pos = 0;
                if (!buscar_usuario(nom_desbloq, &u, &pos)) {
                    printf("Usuario '%s' no encontrado.\n", nom_desbloq);
                    break;
                }
                if (!u.bloqueado && u.intentos_fallidos == 0) {
                    printf("El usuario '%s' ya se encuentra activo y sin bloqueos.\n", nom_desbloq);
                    break;
                }
                u.bloqueado = 0;
                u.intentos_fallidos = 0;
                guardar_usuario(&u, pos);
                auditar("DESBLOQUEAR", nom_desbloq, "OK");
                printf("Usuario '%s' desbloqueado exitosamente. Contador de fallos restablecido a 0.\n", nom_desbloq);
                break;
            }
            case 0:
                return;
            default:
                if (feof(stdin)) return;
                printf("Opción inválida.\n");
        }
    }
}

void seg_cambiar_clave(void) {
    if (!hay_sesion) return;

    printf("\n--- CAMBIAR MI CONTRASEÑA (%s) ---\n", sesion.nombre);
    char clave_actual[64], clave_nueva[64], clave_conf[64];

    leer_clave("Contraseña actual: ", clave_actual, sizeof clave_actual);
    if (hash_clave(clave_actual, sesion.sal) != sesion.hash) {
        printf("Contraseña actual incorrecta. Operación cancelada.\n");
        auditar("CAMBIO_CLAVE", sesion.nombre, "CLAVE_INCORRECTA");
        return;
    }

    leer_clave("Nueva contraseña (mínimo 6 caracteres): ", clave_nueva, sizeof clave_nueva);
    if (strlen(clave_nueva) < 6) {
        printf("Error: La contraseña debe tener al menos 6 caracteres.\n");
        auditar("CAMBIO_CLAVE", sesion.nombre, "LONGITUD_INSUFICIENTE");
        return;
    }

    leer_clave("Confirmar nueva contraseña: ", clave_conf, sizeof clave_conf);
    if (strcmp(clave_nueva, clave_conf) != 0) {
        printf("Error: Las contraseñas no coinciden.\n");
        return;
    }

    /* Generar nueva sal aleatoria y nuevo hash */
    sesion.sal = ((unsigned long)rand() << 16) ^ (unsigned long)time(NULL) ^ (unsigned long)getpid();
    sesion.hash = hash_clave(clave_nueva, sesion.sal);

    Usuario u;
    off_t pos = 0;
    if (buscar_usuario(sesion.nombre, &u, &pos)) {
        u.sal = sesion.sal;
        u.hash = sesion.hash;
        guardar_usuario(&u, pos);
    }

    auditar("CAMBIO_CLAVE", sesion.nombre, "OK");
    printf("¡Contraseña actualizada exitosamente! Se ha generado una nueva sal criptográfica.\n");
}

void seg_guardar_matriz(void) {
    mkdir("datos", 0755);
    int fd = open(ARCH_MATRIZ, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { perror("seg_guardar_matriz: open"); return; }
    chmod(ARCH_MATRIZ, 0600);
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
        printf("matriz.dat con formato anterior o dañada: regenerando matriz v3.0 con 9 objetos.\n");
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
    printf("\n=============================== MATRIZ DE CONTROL DE ACCESO ===============================\n");
    printf("%-10s | %-6s | %-6s | %-6s | %-6s | %-6s | %-6s | %-6s | %-6s | %-6s\n",
           "Dominio", "Regist", "Juegos", "Partid", "Proces", "Matriz", "Sistem", "Usuari", "Bitaco", "Virtua");
    printf("-----------+--------+--------+--------+--------+--------+--------+--------+--------+---------\n");
    for (int r = 0; r < NUM_ROLES; r++) {
        char c[NUM_OBJETOS][8];
        for (int o = 0; o < NUM_OBJETOS; o++) {
            derechos_a_cadena(matriz[r][o], c[o]);
        }
        printf("%-10s | %-6s | %-6s | %-6s | %-6s | %-6s | %-6s | %-6s | %-6s | %-6s\n",
               NOMBRE_ROL[r], c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8]);
    }
    printf("Leyenda: C=Create (1), R=Read (2), U=Update (4), D=Delete (8), X=Exec (16), -=Denegado\n");
    printf("============================================================================================\n");
}

void seg_modificar_matriz(void) {
    if (!seg_verificar(OBJ_MATRIZ, D_UPDATE)) {
        return;
    }
    printf("\n--- MODIFICACIÓN DE LA MATRIZ DE ACCESO ---\n");
    int r = leer_entero("Rol a modificar (0=admin, 1=operador, 2=invitado): ");
    if (r < 0 || r >= NUM_ROLES) { printf("Rol inválido.\n"); return; }

    printf("Objetos: 0=reg, 1=juegos, 2=partidas, 3=procesos, 4=matriz, 5=sistema,\n");
    printf("         6=usuarios, 7=bitacora, 8=virtualizacion\n");
    int o = leer_entero("Objeto a modificar (0 a 8): ");
    if (o < 0 || o >= NUM_OBJETOS) { printf("Objeto inválido.\n"); return; }

    printf("Modificando derechos de rol '%s' sobre objeto '%s'...\n", NOMBRE_ROL[r], NOMBRE_OBJ[o]);
    printf("Introduzca nuevos derechos en bits (Suma: C=1, R=2, U=4, D=8, X=16 | 0=Ninguno):\n");
    int bits = leer_entero("Valor decimal de derechos (0 a 31): ");
    if (bits < 0 || bits > 31) { printf("Valor de derechos inválido.\n"); return; }

    /* Blindaje de seguridad: el admin no puede quitarse control de la matriz */
    if (r == ROL_ADMIN && o == OBJ_MATRIZ && !(bits & D_UPDATE)) {
        printf("Error de Seguridad: El Administrador no puede removerse a sí mismo el control de la matriz.\n");
        return;
    }

    matriz[r][o] = (unsigned char)bits;
    seg_guardar_matriz();
    auditar("MODIF_MATRIZ", NOMBRE_OBJ[o], "OK");
    printf("Matriz actualizada exitosamente y persistida en disco con permisos 0600.\n");
}
