#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "virtualizacion.h"
#include "kernel.h"

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

static void leer_dmi(const char *campo, char *buf, int n) {
    char ruta[96];
    snprintf(ruta, sizeof ruta, "/sys/class/dmi/id/%s", campo);
    FILE *f = fopen(ruta, "r");
    strncpy(buf, "(no disponible)", n - 1);
    buf[n - 1] = '\0';
    if (f) {
        if (fgets(buf, n, f)) {
            buf[strcspn(buf, "\r\n")] = '\0';
        }
        fclose(f);
    }
}

static int buscar_cpuinfo_flag(const char *flag) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return 0;
    char linea[2048];
    int encontrado = 0;
    size_t flen = strlen(flag);

    while (fgets(linea, sizeof linea, f)) {
        if (strncmp(linea, "flags", 5) == 0 || strncmp(linea, "Features", 8) == 0) {
            char *p = strstr(linea, flag);
            while (p) {
                int ant_ok = (p == linea || *(p - 1) == ' ' || *(p - 1) == '\t' || *(p - 1) == ':');
                int post_ok = (*(p + flen) == ' ' || *(p + flen) == '\t' || *(p + flen) == '\n' || *(p + flen) == '\r' || *(p + flen) == '\0');
                if (ant_ok && post_ok) {
                    encontrado = 1;
                    break;
                }
                p = strstr(p + 1, flag);
            }
            if (encontrado) break;
        }
    }
    fclose(f);
    return encontrado;
}

void virt_info(void) {
    printf("\n=================== MÓDULO DE INFORMACIÓN DE VIRTUALIZACIÓN ===================\n");
    char cpu_fab[32] = "Desconocido";
    int en_vm = 0;
    int vtx_expuesto = 0;
    int amdv_expuesto = 0;
    char firma[32] = "(no disponible)";

#if defined(__x86_64__) || defined(__i386__)
    unsigned int a = 0, b = 0, c = 0, d = 0;

    /* Hoja 0: Fabricante del procesador */
    if (__get_cpuid(0, &a, &b, &c, &d)) {
        char fab[13] = {0};
        memcpy(fab, &b, 4);
        memcpy(fab + 4, &d, 4);
        memcpy(fab + 8, &c, 4);
        strncpy(cpu_fab, fab, sizeof(cpu_fab) - 1);
    }
    printf("Fabricante de la CPU   : %s\n", cpu_fab);

    /* Hoja 1: Capacidades - Bit Hypervisor y VT-x (VMX) */
    if (__get_cpuid(1, &a, &b, &c, &d)) {
        en_vm = (c >> 31) & 1;           /* ECX[31] = bit hypervisor */
        vtx_expuesto = (c >> 5) & 1;     /* ECX[5]  = VMX (VT-x) */
    }
    printf("Dentro de una VM       : %s\n", en_vm ? "SÍ" : "NO");
    printf("VT-x expuesto (Intel)  : %s\n", vtx_expuesto ? "sí" : "no");

    /* Hoja 0x80000001: AMD-V (SVM: bit 2 de ECX) */
    unsigned int max_ext = 0;
    __get_cpuid(0x80000000, &max_ext, &b, &c, &d);
    if (max_ext >= 0x80000001) {
        __get_cpuid(0x80000001, &a, &b, &c, &d);
        amdv_expuesto = (c >> 2) & 1;    /* ECX[2] = SVM (AMD-V) */
    }
    printf("AMD-V expuesto (AMD)   : %s\n", amdv_expuesto ? "sí" : "no");

    /* Hoja 0x40000000: Firma del hipervisor */
    if (en_vm) {
        char f[13] = {0};
        __cpuid(0x40000000, a, b, c, d);
        memcpy(f, &b, 4);
        memcpy(f + 4, &c, 4);
        memcpy(f + 8, &d, 4);
        strncpy(firma, f, sizeof(firma) - 1);
        printf("Firma del hipervisor   : %s\n", firma);
    } else {
        printf("Firma del hipervisor   : (no aplica / bare-metal)\n");
    }
#else
    printf("CPUID solo existe en procesadores x86 / x86_64.\n");
#endif

    /* Paginación anidada: EPT (Intel) y NPT (AMD) en /proc/cpuinfo */
    int hay_ept = buscar_cpuinfo_flag("ept");
    int hay_npt = buscar_cpuinfo_flag("npt");
    printf("EPT (Paginación Anidada Intel): %s\n", hay_ept ? "sí (presente)" : "no (no expuesto/ausente)");
    printf("NPT (Paginación Anidada AMD)  : %s\n", hay_npt ? "sí (presente)" : "no (no expuesto/ausente)");

    /* Datos DMI (/sys/class/dmi/id/) */
    char fab[96], prod[96];
    leer_dmi("sys_vendor", fab, sizeof fab);
    leer_dmi("product_name", prod, sizeof prod);
    printf("DMI (Fabricante / Producto)   : %s / %s\n", fab, prod);

    /* systemd-detect-virt */
    printf("systemd-detect-virt           : ");
    fflush(stdout);
    system("systemd-detect-virt 2>/dev/null || echo no disponible");

    /* Conclusión en una frase */
    printf("--------------------------------------------------------------------------------\n");
    const char *nom_cpu = (strstr(cpu_fab, "Intel") != NULL) ? "Intel" :
                          ((strstr(cpu_fab, "AMD") != NULL) ? "AMD" : cpu_fab);

    if (en_vm) {
        const char *hv_nombre = "Hipervisor Genérico";
        const char *tipo_str = "tipo 2 (alojado)";

        if (strstr(firma, "VBox") || strstr(fab, "innotek") || strstr(prod, "VirtualBox")) {
            hv_nombre = "VirtualBox";
            tipo_str = "tipo 2";
        } else if (strstr(firma, "VMware") || strstr(fab, "VMware") || strstr(prod, "VMware")) {
            hv_nombre = "VMware";
            tipo_str = "tipo 2";
        } else if (strstr(firma, "KVM") || strstr(prod, "KVM") || strstr(prod, "Standard PC")) {
            hv_nombre = "KVM";
            tipo_str = "tipo 1";
        } else if (strstr(firma, "Microsoft") || strstr(fab, "Microsoft")) {
            hv_nombre = "Hyper-V / WSL2";
            tipo_str = "tipo 1";
        } else if (strstr(firma, "Xen")) {
            hv_nombre = "Xen";
            tipo_str = "tipo 1";
        }

        printf("Conclusión: MiniOS corre dentro de %s, hipervisor %s, sobre un procesador %s.\n",
               hv_nombre, tipo_str, nom_cpu);
    } else {
        printf("Conclusión: MiniOS corre directamente sobre hardware físico (bare-metal), sobre un procesador %s.\n",
               nom_cpu);
    }
    printf("================================================================================\n");
}
