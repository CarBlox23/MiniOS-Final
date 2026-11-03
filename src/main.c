#include <stdio.h>
#include "kernel.h"
#include "fs.h"
#include "proc.h"
#include "juego.h"
#include "seguridad.h"
#include "sistema.h"

static void mostrar_menu(void) {
    printf("\n==================== MiniOS v%s | Sesión: %s ====================\n",
           VERSION_MINIOS, seg_usuario_actual());
    printf(" 1. Create (Registro)       2. Read (Listar)         3. Update (Registro)\n");
    printf(" 4. Delete (Registro)       5. Estado del Disco      6. Gestión de Procesos\n");
    printf(" 7. Centro de Videojuego    8. Ver Matriz Acceso     9. Modificar Matriz\n");
    printf(" 10. Acerca del Sistema     11. Cambiar de Usuario   0. Apagar Sistema\n");
    printf("=========================================================================\n");
}

int main(void) {
    if (fs_montar(DIR_DATOS "minios.disk") != 0) {
        printf("Fallo crítico al inicializar disco virtual.\n");
        return 1;
    }
    seg_cargar_matriz();
    seg_iniciar_sesion();

    while (1) {
        mostrar_menu();
        switch (leer_entero(" Ingrese Opción: ")) {
            case 1:
                if (seg_verificar(OBJ_REGISTROS, D_CREATE)) fs_crear();
                break;
            case 2:
                if (seg_verificar(OBJ_REGISTROS, D_READ)) fs_listar();
                break;
            case 3:
                if (seg_verificar(OBJ_REGISTROS, D_UPDATE)) fs_actualizar();
                break;
            case 4:
                if (seg_verificar(OBJ_REGISTROS, D_DELETE)) fs_borrar();
                break;
            case 5:
                if (seg_verificar(OBJ_REGISTROS, D_READ)) fs_estado();
                break;
            case 6:
                proc_menu();
                break;
            case 7:
                juego_menu();
                break;
            case 8:
                if (seg_verificar(OBJ_MATRIZ, D_READ)) seg_ver_matriz();
                break;
            case 9:
                if (seg_verificar(OBJ_MATRIZ, D_UPDATE)) seg_modificar_matriz();
                break;
            case 10:
                acerca_de();
                break;
            case 11:
                seg_iniciar_sesion();
                break;
            case 0:
                if (seg_verificar(OBJ_SISTEMA, D_EXEC)) {
                    fs_desmontar();
                    printf("\nApagando MiniOS v3.0 de forma segura. ¡Hasta luego!\n");
                    return 0;
                }
                break;
            default:
                if (feof(stdin)) {
                    fs_desmontar();
                    printf("\n[EOF detectado] Apagado de emergencia seguro.\n");
                    return 0;
                }
                printf("Opción inválida. Intente de nuevo.\n");
        }
    }
}
