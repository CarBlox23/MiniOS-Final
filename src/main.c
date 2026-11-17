#include <stdio.h>
#include "kernel.h"
#include "fs.h"
#include "proc.h"
#include "juego.h"
#include "seguridad.h"
#include "bitacora.h"
#include "virtualizacion.h"
#include "sistema.h"

static void mostrar_menu(void) {
    printf("\n==================== MiniOS v%s | Usuario: %s (%s) ====================\n",
           VERSION_MINIOS, seg_usuario_actual(), seg_rol_actual());
    printf(" 1. Create (Registro)        2. Read (Listar)          3. Update (Registro)\n");
    printf(" 4. Delete (Registro)        5. Estado del Disco       6. Gestión de Procesos\n");
    printf(" 7. Centro de Videojuego     8. Ver Bitácora           9. Ver Matriz Acceso\n");
    printf(" 10. Modificar Matriz        11. Virtualización        12. Cerrar Sesión\n");
    printf(" 13. Gestionar Usuarios      14. Cambiar Contraseña    15. Acerca del Sistema\n");
    printf("  0. Apagar Sistema\n");
    printf("===============================================================================\n");
}

int main(void) {
    if (fs_montar(DIR_DATOS "minios.disk") != 0) {
        printf("Fallo crítico al inicializar almacenamiento RAID 1.\n");
        return 1;
    }
    seg_cargar_matriz();
    auditar("ARRANQUE", "sistema", "OK");

    while (1) {
        /* Bucle de autenticación previa */
        while (!seg_login()) {
            if (feof(stdin)) {
                fs_desmontar();
                return 0;
            }
        }

        int en_sesion = 1;
        /* Bucle de interacción durante la sesión del usuario */
        while (en_sesion) {
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
                    if (seg_verificar(OBJ_BITACORA, D_READ)) bitacora_mostrar();
                    break;
                case 9:
                    if (seg_verificar(OBJ_MATRIZ, D_READ)) seg_ver_matriz();
                    break;
                case 10:
                    if (seg_verificar(OBJ_MATRIZ, D_UPDATE)) seg_modificar_matriz();
                    break;
                case 11:
                    if (seg_verificar(OBJ_VIRTUALIZACION, D_EXEC)) virt_info();
                    break;
                case 12:
                    seg_cerrar_sesion();
                    en_sesion = 0;
                    break;
                case 13:
                    seg_gestionar_usuarios();
                    break;
                case 14:
                    seg_cambiar_clave();
                    break;
                case 15:
                    acerca_de();
                    break;
                case 0:
                    if (seg_verificar(OBJ_SISTEMA, D_EXEC)) {
                        auditar("APAGADO", "sistema", "OK");
                        fs_desmontar();
                        printf("\nApagando MiniOS v3.0 de forma segura. ¡Hasta luego!\n");
                        return 0;
                    }
                    break;
                default:
                    if (feof(stdin)) {
                        fs_desmontar();
                        printf("\n[EOF detectado] Apagado seguro.\n");
                        return 0;
                    }
                    printf("Opción inválida. Intente de nuevo.\n");
            }
        }
    }
}
