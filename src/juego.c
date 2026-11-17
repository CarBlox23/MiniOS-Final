#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include "kernel.h"
#include "juego.h"
#include "fs.h"
#include "seguridad.h"
#include "bitacora.h"

#define ANCHO 20
#define ALTO  12

static struct termios original;
static int en_modo_raw = 0;
static volatile sig_atomic_t g_sigint_recibido = 0;

/* Estado compartido protegido para autoguardado concurrente (Mejora 9.1) */
static Partida         partida;
static pthread_mutex_t mutex_partida = PTHREAD_MUTEX_INITIALIZER;
static volatile int    jugando = 0;

static void manejador_sigint_juego(int sig) {
    (void)sig;
    g_sigint_recibido = 1;
}

void modo_juego(int activar) {
    if (activar) {
        if (!en_modo_raw) {
            tcgetattr(STDIN_FILENO, &original);
            struct termios t = original;
            t.c_lflag &= ~(ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &t);
            en_modo_raw = 1;
            g_sigint_recibido = 0;
            signal(SIGINT, manejador_sigint_juego);
        }
    } else {
        if (en_modo_raw) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original);
            en_modo_raw = 0;
            signal(SIGINT, SIG_DFL);
        }
    }
}

int tecla(int ms) {
    fd_set fds;
    struct timeval tv = { 0, ms * 1000 };
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) return c;
    }
    return -1;
}

int guardar_partida(const Partida *p) {
    char ruta[128];
    mkdir("datos", 0755);
    snprintf(ruta, sizeof ruta, DIR_DATOS "partida_%s.sav", p->jugador);
    int fd = open(ruta, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    chmod(ruta, 0600);
    ssize_t n = write(fd, p, sizeof *p);
    fsync(fd);
    close(fd);
    return n == (ssize_t)sizeof *p ? 0 : -1;
}

int cargar_partida(const char *jugador, Partida *p) {
    char ruta[128];
    snprintf(ruta, sizeof ruta, DIR_DATOS "partida_%s.sav", jugador);
    int fd = open(ruta, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, p, sizeof *p);
    close(fd);
    return n == (ssize_t)sizeof *p ? 0 : -1;
}

/* Hilo de autoguardado concurrente cada 15 segundos (Mejora 9.1) */
static void *hilo_autoguardado(void *arg) {
    int segundos = *(int *)arg;
    while (jugando) {
        for (int i = 0; i < segundos && jugando; i++) {
            sleep(1);
        }
        if (!jugando) break;

        Partida copia;
        pthread_mutex_lock(&mutex_partida);     /* Sección crítica: lectura atómica del estado */
        copia = partida;
        pthread_mutex_unlock(&mutex_partida);

        copia.guardada_en = time(NULL);
        guardar_partida(&copia);                /* Escritura I/O fuera del cerrojo para fluidez */
    }
    return NULL;
}

static void dibujar_juego(const Partida *p) {
    printf("\033[H\033[J");
    printf("=== SNAKE MINIOS v3.0 | Jugador: %s | Puntos: %d (Autoguardado: Activo cada 15s) ===\n",
           p->jugador, p->puntaje);
    for (int i = 0; i < ANCHO + 2; i++) printf("#");
    printf("\n");
    for (int y = 0; y < ALTO; y++) {
        printf("#");
        for (int x = 0; x < ANCHO; x++) {
            if (x == p->cuerpo[0].x && y == p->cuerpo[0].y) {
                printf("O");
            } else if (x == p->comida.x && y == p->comida.y) {
                printf("*");
            } else {
                int es_cola = 0;
                for (int k = 1; k < p->largo; k++) {
                    if (p->cuerpo[k].x == x && p->cuerpo[k].y == y) {
                        es_cola = 1;
                        break;
                    }
                }
                printf("%c", es_cola ? 'o' : ' ');
            }
        }
        printf("#\n");
    }
    for (int i = 0; i < ANCHO + 2; i++) printf("#");
    printf("\n[W/A/S/D]: Mover | [G]: Guardar y Salir | [Q]: Salir sin guardar\n");
}

static void ejecutar_partida(Partida *p) {
    pthread_mutex_lock(&mutex_partida);
    partida = *p;
    pthread_mutex_unlock(&mutex_partida);

    /* Iniciar hilo concurrente de autoguardado */
    int cada = 15;
    pthread_t hilo;
    jugando = 1;
    pthread_create(&hilo, NULL, hilo_autoguardado, &cada);

    modo_juego(1);
    while (1) {
        if (g_sigint_recibido) {
            modo_juego(0);
            jugando = 0;
            pthread_join(hilo, NULL);
            printf("\n\n[MiniOS] Interrupción detectada (Ctrl+C). Saliendo del juego...\n");
            if (seg_verificar(OBJ_PARTIDAS, D_CREATE | D_UPDATE)) {
                pthread_mutex_lock(&mutex_partida);
                partida.guardada_en = time(NULL);
                Partida copia = partida;
                pthread_mutex_unlock(&mutex_partida);
                if (guardar_partida(&copia) == 0) {
                    printf("[MiniOS] Partida auto-guardada exitosamente en datos/partida_%s.sav\n", copia.jugador);
                    auditar("AUTOGUARDADO_SIGINT", copia.jugador, "OK");
                }
            } else {
                printf("[MiniOS] Aviso: Rol actual sin permisos para guardar partidas.\n");
            }
            return;
        }

        Partida copia_dibujo;
        pthread_mutex_lock(&mutex_partida);
        copia_dibujo = partida;
        pthread_mutex_unlock(&mutex_partida);

        dibujar_juego(&copia_dibujo);
        int c = tecla(180);

        if (g_sigint_recibido) continue;

        if (c == 'q' || c == 'Q') {
            break;
        }

        if (c == 'g' || c == 'G') {
            if (!seg_verificar(OBJ_PARTIDAS, D_CREATE | D_UPDATE)) {
                modo_juego(0);
                printf("\n\n  ACCESO DENEGADO: El usuario '%s' no tiene derechos para guardar partidas en disco.\n",
                       seg_usuario_actual());
                printf("  Presione [ENTER] para reanudar el juego o 'Q' para salir sin guardar...\n");
                char tmp[16];
                leer_linea("", tmp, sizeof tmp);
                modo_juego(1);
                continue;
            }
            modo_juego(0);
            jugando = 0;
            pthread_join(hilo, NULL);

            pthread_mutex_lock(&mutex_partida);
            partida.guardada_en = time(NULL);
            Partida copia = partida;
            pthread_mutex_unlock(&mutex_partida);

            if (guardar_partida(&copia) == 0) {
                printf("\nPartida guardada con éxito en datos/partida_%s.sav\n", copia.jugador);
                auditar("GUARDAR_PARTIDA", copia.jugador, "OK");
            } else {
                printf("\nError al guardar la partida en disco.\n");
            }
            return;
        }

        pthread_mutex_lock(&mutex_partida);
        if ((c == 'w' || c == 'W') && partida.direccion != 1) partida.direccion = 0;
        if ((c == 's' || c == 'S') && partida.direccion != 0) partida.direccion = 1;
        if ((c == 'a' || c == 'A') && partida.direccion != 3) partida.direccion = 2;
        if ((c == 'd' || c == 'D') && partida.direccion != 2) partida.direccion = 3;

        for (int i = partida.largo - 1; i > 0; i--) partida.cuerpo[i] = partida.cuerpo[i - 1];
        if (partida.direccion == 0) partida.cuerpo[0].y--;
        if (partida.direccion == 1) partida.cuerpo[0].y++;
        if (partida.direccion == 2) partida.cuerpo[0].x--;
        if (partida.direccion == 3) partida.cuerpo[0].x++;

        /* Colisión con bordes */
        if (partida.cuerpo[0].x < 0 || partida.cuerpo[0].x >= ANCHO ||
            partida.cuerpo[0].y < 0 || partida.cuerpo[0].y >= ALTO) {
            int puntaje_final = partida.puntaje;
            char jugador_fin[32];
            strncpy(jugador_fin, partida.jugador, sizeof(jugador_fin) - 1);
            jugador_fin[sizeof(jugador_fin) - 1] = '\0';
            pthread_mutex_unlock(&mutex_partida);

            modo_juego(0);
            jugando = 0;
            pthread_join(hilo, NULL);

            printf("\n¡GAME OVER! Has chocado contra el muro. Puntaje final: %d\n", puntaje_final);
            if (seg_verificar(OBJ_REGISTROS, D_CREATE)) {
                fs_registrar_record(jugador_fin, puntaje_final);
                printf("[MiniOS] Récord registrado con éxito en disco virtual.\n");
            } else {
                printf("[MiniOS] Récord no guardado: Rol sin permisos de creación en disco virtual.\n");
            }
            char ruta[128];
            snprintf(ruta, sizeof ruta, DIR_DATOS "partida_%s.sav", jugador_fin);
            unlink(ruta);
            return;
        }

        /* Colisión con comida */
        if (partida.cuerpo[0].x == partida.comida.x && partida.cuerpo[0].y == partida.comida.y) {
            partida.puntaje += 10;
            if (partida.largo < MAX_COLA - 1) partida.largo++;
            partida.comida.x = rand() % ANCHO;
            partida.comida.y = rand() % ALTO;
        }
        pthread_mutex_unlock(&mutex_partida);
    }

    modo_juego(0);
    jugando = 0;
    pthread_join(hilo, NULL);
}

void juego_menu(void) {
    while (1) {
        printf("\n--- MiniOS v3.0 | VIDEOJUEGO INTEGRADO (SNAKE) ---\n");
        printf(" 1. Nueva Partida\n");
        printf(" 2. Continuar Partida Guardada\n");
        printf(" 0. Volver\n");
        switch (leer_entero(" Opción juego: ")) {
            case 1: {
                if (!seg_verificar(OBJ_JUEGOS, D_EXEC)) break;
                Partida p;
                memset(&p, 0, sizeof p);
                strncpy(p.jugador, seg_usuario_actual(), sizeof(p.jugador) - 1);
                p.jugador[sizeof(p.jugador) - 1] = '\0';
                p.puntaje = 0;
                p.nivel = 1;
                p.direccion = 3;
                p.largo = 3;
                p.cuerpo[0].x = 5; p.cuerpo[0].y = 5;
                p.cuerpo[1].x = 4; p.cuerpo[1].y = 5;
                p.cuerpo[2].x = 3; p.cuerpo[2].y = 5;
                p.comida.x = 10;   p.comida.y = 5;
                ejecutar_partida(&p);
                break;
            }
            case 2: {
                if (!seg_verificar(OBJ_PARTIDAS, D_READ)) break;
                Partida p;
                if (cargar_partida(seg_usuario_actual(), &p) == 0) {
                    printf("Cargando partida guardada de %s (Puntaje previo: %d)...\n", p.jugador, p.puntaje);
                    ejecutar_partida(&p);
                } else {
                    printf("No existe partida guardada para el usuario '%s'.\n", seg_usuario_actual());
                }
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
