#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>
#include "sistema.h"

void acerca_de(void) {
    struct utsname u;
    if (uname(&u) != 0) {
        perror("uname");
        return;
    }
    printf("\n===== ACERCA DE MINIOS =====\n");
    printf("MiniOS v3.0 sobre %s %s (%s)\n", u.sysname, u.release, u.machine);
    printf("Núcleos CPU   : %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    printf("PID MiniOS    : %d\n", getpid());
    printf("UID Linux     : %d\n", getuid());
    printf("============================\n");
}
