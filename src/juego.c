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
#include "kernel.h"
#include "juego.h"
#include "fs.h"
#include "seguridad.h"

#define ANCHO 20
#define ALTO  12

static struct termios original;
static int en_modo_raw = 0;
static Partida partida_activa;

static void manejador_sigint_juego(int sig) {
    (void)sig;
    if (en_modo_raw) {
        guardar_partida(&partida_activa);
        modo_juego(0);
        printf("\n[Partida auto-guardada ante interrupción Ctrl+C]\n");
    }
    exit(0);
}

void modo_juego(int activar) {
    if (activar) {
        if (!en_modo_raw) {
            tcgetattr(STDIN_FILENO, &original);
            struct termios t = original;
            t.c_lflag &= ~(ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &t);
            en_modo_raw = 1;
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
    snprintf(ruta, sizeof ruta, "datos/partida_%s.sav", p->jugador);
    int fd = open(ruta, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    ssize_t n = write(fd, p, sizeof *p);
    fsync(fd);
    close(fd);
    return n == (ssize_t)sizeof *p ? 0 : -1;
}

int cargar_partida(const char *jugador, Partida *p) {
    char ruta[128];
    snprintf(ruta, sizeof ruta, "datos/partida_%s.sav", jugador);
    int fd = open(ruta, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, p, sizeof *p);
    close(fd);
    return n == (ssize_t)sizeof *p ? 0 : -1;
}

static void dibujar_juego(const Partida *p) {
    printf("\033[H\033[J");
    printf("=== SNAKE MINIOS v3.0 | Jugador: %s | Puntos: %d ===\n", p->jugador, p->puntaje);
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
    printf("\n[W/A/S/D]: Mover | [G]: Guardar y Salir | [Q]: Salir\n");
}

static void ejecutar_partida(Partida *p) {
    modo_juego(1);
    partida_activa = *p;
    while (1) {
        dibujar_juego(p);
        int c = tecla(180);
        if (c == 'q' || c == 'Q') break;
        if (c == 'g' || c == 'G') {
            p->guardada_en = time(NULL);
            guardar_partida(p);
            modo_juego(0);
            printf("\nPartida guardada con éxito en datos/partida_%s.sav\n", p->jugador);
            return;
        }
        if ((c == 'w' || c == 'W') && p->direccion != 1) p->direccion = 0;
        if ((c == 's' || c == 'S') && p->direccion != 0) p->direccion = 1;
        if ((c == 'a' || c == 'A') && p->direccion != 3) p->direccion = 2;
        if ((c == 'd' || c == 'D') && p->direccion != 2) p->direccion = 3;

        for (int i = p->largo - 1; i > 0; i--) p->cuerpo[i] = p->cuerpo[i - 1];
        if (p->direccion == 0) p->cuerpo[0].y--;
        if (p->direccion == 1) p->cuerpo[0].y++;
        if (p->direccion == 2) p->cuerpo[0].x--;
        if (p->direccion == 3) p->cuerpo[0].x++;

        // Colisión con paredes
        if (p->cuerpo[0].x < 0 || p->cuerpo[0].x >= ANCHO ||
            p->cuerpo[0].y < 0 || p->cuerpo[0].y >= ALTO) {
            modo_juego(0);
            printf("\n¡GAME OVER! Has chocado contra el muro. Puntaje final: %d\n", p->puntaje);
            fs_registrar_record(p->jugador, p->puntaje);
            char ruta[128];
            snprintf(ruta, sizeof ruta, "datos/partida_%s.sav", p->jugador);
            unlink(ruta);
            return;
        }

        // Colisión con comida
        if (p->cuerpo[0].x == p->comida.x && p->cuerpo[0].y == p->comida.y) {
            p->puntaje += 10;
            if (p->largo < MAX_COLA - 1) p->largo++;
            p->comida.x = rand() % ANCHO;
            p->comida.y = rand() % ALTO;
        }

        partida_activa = *p;
    }
    modo_juego(0);
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
                    printf("Cargando partida guardada de %s...\n", p.jugador);
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
