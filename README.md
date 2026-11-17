# MiniOS v3.0 — Sistema Operativo Simulado en C (Parte 2)

## 👥 Integrantes del Proyecto
- **Juan Camilo y Equipo de Desarrollo**
- Estudiantes de Ingeniería de Sistemas — Sistemas Operativos

---

## 🚀 Resumen del Proyecto y Novedades en MiniOS v3.0

MiniOS v3.0 concluye el ciclo completo de desarrollo del sistema operativo simulado. Integra la gestión de almacenamiento, planificación de procesos, concurrencia, protección multinivel, auditoría forense, virtualización y redundancia en almacenamiento secundario.

### 📋 Resumen de Mejoras Incorporadas:
| # | Mejora | Tema del Curso | Descripción Técnica |
| :---: | :--- | :--- | :--- |
| **6** | **Login con Contraseñas y Bloqueo** | Protección y Control de Acceso | Autenticación previa obligatoria, almacenamiento en `usuarios.dat` con hash y sal aleatoria (`djb2`), eco desactivado en terminal (`termios`), bloqueo automático a los 3 intentos fallidos, gestión administrativa de usuarios y cambio de contraseñas. |
| **7** | **Bitácora de Auditoría Forense** | Supervisión y Auditoría | Registro continuo en `datos/bitacora.log` bajo mediación completa en Capa 2, resumen estadístico automático (accesos denegados, fallos de login, usuario con más denegaciones) y alerta en pantalla tras 5 denegaciones en sesión. |
| **8** | **Módulo de Virtualización** | Virtualización de Sistemas | Inspección de hardware y CPUID: fabricante de CPU, bit hypervisor, firma de hipervisor, detección de VT-x / AMD-V, paginación anidada EPT/NPT, DMI y `systemd-detect-virt` con conclusión deductiva. |
| **9.1** | **Autoguardado Concurrente** | Sincronización de Procesos/Hilos | Hilo POSIX en segundo plano que persiste la partida cada 15 segundos protegiendo la estructura compartida mediante exclusión mutua (`pthread_mutex_t`), tolerante a `kill -9`. |
| **9.2** | **Disco Espejo — RAID 1** | Memoria Secundaria | Replicación síncrona en `minios.disk` y `minios_espejo.disk` mediante `fsync()`. Detección de apagado sucio y autoreconstrucción ante corrupción o borrado de cualquiera de los discos. |
| **9.3** | **Monitor de Memoria y Prioridades** | Memoria Principal y Planificación | Inspección de memoria virtual (`VmSize`), memoria residente física (`VmRSS`) y cálculo de marcos de página en RAM; asignación y modificación dinámica de prioridades con `nice` (`setpriority`). |

---

## 🏛️ Arquitectura Jerárquica por Capas Actualizada

MiniOS v3.0 mantiene una estricta separación de responsabilidades. La supervisión y la auditoría se integran directamente en la **Capa 2** junto a la matriz de control de acceso, garantizando que el principio de mediación completa audite automáticamente cada acción sin saturar el código del menú:

```
[ Capa 4: Interfaz de Usuario ]  ──> main.c (Bucle de login previo + bucle de sesión activa)
               │
               ▼
[ Capa 3: Servicios del SO ]     ──> proc.c, juego.c, sistema.c, virtualizacion.c
               │
               ▼
[ Capa 2: Protección y Acceso ]  ──> seguridad.c (Matriz de derechos, sal/hash, sesiones)
               │                 ──> bitacora.c  (Auditoría continua, alertas y mediación)
               ▼
[ Capa 1: Almacenamiento RAID 1] ──> fs.c (minios.disk + minios_espejo.disk, superbloque, ranuras)
               │
               ▼
[ Capa 0: Kernel Linux POSIX ]   ──> System Calls: open, read, write, lseek, fsync, fork,
                                     exec*, waitpid, kill, tcsetattr, select, cpuid, pthreads...
```

---

## ⚙️ Compilación y Ejecución en Kali Linux

El proyecto cuenta con un `Makefile` compatible con estándares POSIX y `-pthread`:

```bash
# 1. Compilación limpia y estricta (cero advertencias con -Wall -Wextra)
make clean && make

# 2. Ejecutar MiniOS v3.0
./minios

# 3. Limpieza profunda (elimina binarios y archivos de datos temporales)
make distclean
```

> [!IMPORTANT]
> **Usuarios y Contraseñas por Defecto:**
> Al arrancar por primera vez, MiniOS inicializa automáticamente tres usuarios:
> - **Administrador:** `admin` / clave: `admin123`
> - **Operador:** `operador` / clave: `oper123`
> - **Invitado:** `invitado` / clave: `inv123`
> 
> Por política de seguridad, se recomienda cambiar estas contraseñas inmediatamente tras el primer ingreso utilizando la opción **14. Cambiar Mi Contraseña**.

---

## 🔒 Mejora 6: Control de Acceso al Sistema (Autenticación y Bloqueo)

### 1. Justificación Criptográfica y Almacenamiento Seguro
MiniOS v3.0 **nunca almacena contraseñas en texto claro**. Siguiendo el estándar de `/etc/shadow` de Linux, las credenciales se almacenan en el archivo binario `datos/usuarios.dat` con la siguiente estructura:
- `nombre`: Identificador único del usuario (32 bytes).
- `sal`: Entero aleatorio de 64 bits generado en tiempo de creación (`((rand() << 16) ^ time(NULL) ^ getpid())`). La sal garantiza que dos usuarios con la misma contraseña posean hashes completamente distintos, neutralizando ataques basados en tablas precalculadas (Rainbow Tables).
- `hash`: Resultado de aplicar la función criptográfica `hash_clave(clave, sal)` basada en el algoritmo *djb2*.
- `rol`: Nivel de privilegio (`ROL_ADMIN`, `ROL_OPERADOR`, `ROL_INVITADO`).
- `intentos_fallidos`: Contador de intentos de acceso erróneos consecutivos.
- `bloqueado`: Bandera booleana de estado de la cuenta.

### 2. Ocultamiento de Entrada (Eco de Terminal Desactivado)
Mediante la API `termios` de POSIX (`c_lflag &= ~ECHO`), la función `leer_clave()` desactiva el eco en pantalla mientras el usuario escribe su clave, previniendo ataques de observación directa (shoulder surfing). Si la entrada proviene de un script o tubería (`isatty(STDIN_FILENO) == 0`), el sistema se adapta automáticamente sin bloquearse.

### 3. Respuestas Genéricas y Resistencia a Ataques de Enumeración
Tanto si el nombre de usuario no existe como si la clave ingresada es incorrecta, MiniOS emite exactamente el mismo mensaje:
`"Usuario o contraseña incorrectos."`
Esto impide que un atacante descubra qué nombres de usuario son válidos en el sistema.

### 4. Política de Bloqueo por Fuerza Bruta y Gestión Administrativa
- Tras **3 intentos fallidos**, la cuenta queda bloqueada automáticamente (`u.bloqueado = 1`), impidiendo nuevos accesos aun si se introduce la clave correcta.
- Solo el rol `admin` puede ingresar al menú **13. Gestionar Usuarios** para:
  1. **Listar Usuarios:** Ver estado activo/bloqueado e intentos fallidos sin revelar hashes ni sales.
  2. **Crear Usuarios:** Exigiendo contraseñas de al menos 6 caracteres.
  3. **Desbloquear Cuentas:** Restableciendo el estado activo y el contador de fallos a 0.
- La opción **14. Cambiar Mi Contraseña** permite a cualquier usuario activo renovar su credencial tras verificar su clave actual y exigir una nueva longitud mínima de 6 caracteres.
- Todos los archivos de datos (`usuarios.dat`, `matriz.dat`, `minios.disk`, `minios_espejo.disk`, `bitacora.log`, `partida_*.sav`) se configuran con permisos **`0600` (`-rw-------`)**, accesibles únicamente por el propietario del proceso.

---

## 📜 Mejora 7: Bitácora de Supervisión y Auditoría en Vivo

### 1. Mediación Completa y Registro Transparente
La función `auditar(accion, objeto, resultado)` reside en la Capa 2. Cada invocación a `seg_verificar(obj, derecho)` registra automáticamente el acceso en `datos/bitacora.log`:
```
2026-11-17 14:58:37 | invitado   | invitado | CREATE         | registros  | DENEGADO
2026-11-17 14:58:37 | invitado   | invitado | ALERTA_SEG     | invitado   | 5 DENEGACIONES ACUMULADAS
2026-11-17 14:58:37 | admin      | admin    | READ           | bitacora   | PERMITIDO
```

### 2. Eventos Auditados
- Arranque y apagado seguro del sistema (`ARRANQUE`, `APAGADO`).
- Intentos de acceso (`LOGIN EXITOSO`, `LOGIN FALLIDO`, `BLOQUEO`, `CUENTA BLOQUEADA`, `LOGOUT`).
- Derechos de matriz evaluados (`CREATE`, `READ`, `UPDATE`, `DELETE`, `EXEC`).
- Modificaciones de seguridad (`MODIF_MATRIZ`, `CREAR_USUARIO`, `DESBLOQUEAR`, `CAMBIO_CLAVE`).
- Eventos de almacenamiento (`RAID1_RECONSTR`) y autoguardado de partidas (`AUTOGUARDADO_SIGINT`).

### 3. Resumen Estadístico y Alerta de Intrusión
La opción **8. Ver Bitácora** (exclusiva de `admin`) analiza el archivo y genera un consolidado al final:
- Total de accesos denegados.
- Total de inicios de sesión fallidos.
- Identificación dinámica del **usuario con más denegaciones**.
- **Alerta Reactiva:** Si un usuario acumula **5 denegaciones en una misma sesión**, MiniOS emite una advertencia visual destacada en consola y registra un evento crítico `ALERTA_SEG` en la bitácora.

> [!TIP]
> **Monitoreo en Vivo durante la Sustentación:**
> Puede abrir una segunda terminal en Linux y ejecutar:
> ```bash
> tail -f datos/bitacora.log
> ```
> para observar en tiempo real cómo cada acción realizada en MiniOS se audita instantáneamente.

---

## 🖥️ Mejora 8: Módulo e Informe Teórico de Virtualización

### 1. Módulo de Inspección de Hardware (`virt_info`)
El módulo consulta directamente los registros del procesador y las interfaces del kernel:
- **CPUID Hoja 0:** Obtiene la cadena del fabricante del procesador (`GenuineIntel`, `AuthenticAMD`).
- **CPUID Hoja 1 (Bit Hypervisor):** Lee el bit 31 del registro `ECX`. Si es 1, el código se ejecuta dentro de una máquina virtual.
- **CPUID Hoja 0x40000000:** Lee la firma del hipervisor (`VBoxVBoxVBox` para VirtualBox, `VMwareVMware` para VMware, `Microsoft Hv` para Hyper-V/WSL2, `KVMKVMKVM` para KVM).
- **CPUID Hoja 1, Bit 5 (VMX):** Evalúa si las instrucciones Intel VT-x están expuestas a la máquina virtual.
- **CPUID Hoja 0x80000001, Bit 2 (SVM):** Evalúa si las instrucciones AMD-V están expuestas a la máquina virtual.
- **/proc/cpuinfo (Paginación Anidada):** Escanea los flags de la CPU buscando soporte de paginación anidada por hardware: `ept` (Intel Extended Page Tables) y `npt` (AMD Nested Page Tables).
- **/sys/class/dmi/id/:** Lee el fabricante del sistema (`sys_vendor`) y el nombre del producto (`product_name`).
- **systemd-detect-virt:** Invoca la utilidad nativa de Linux para corroborar la tecnología de virtualización.
- **Frase de Conclusión Deductiva:** Integra todos los indicadores para emitir una síntesis en lenguaje natural (ej. *"MiniOS corre dentro de Hyper-V / WSL2, hipervisor tipo 1, sobre un procesador AMD."* o *"MiniOS corre dentro de VirtualBox, hipervisor tipo 2, sobre un procesador Intel."*).

---

### 2. Informe Teórico de Virtualización

#### A. Clasificación de Hipervisores: Tipo 1 vs Tipo 2
1. **Hipervisor Tipo 1 (Bare-Metal / Nativo):** Se ejecuta directamente sobre el hardware físico sin necesidad de un sistema operativo anfitrión subyacente. Asume el control total de los recursos de la CPU, la memoria y los dispositivos de E/S.
   - *Ejemplos:* VMware ESXi, Microsoft Hyper-V, Xen y KVM (Kernel-based Virtual Machine, integrado en Linux).
   - *Rendimiento:* Máxima eficiencia y mínima sobrecarga (overhead), ideal para centros de datos y computación en la nube.
2. **Hipervisor Tipo 2 (Hosted / Alojado):** Se ejecuta como un proceso de aplicación ordinario sobre un sistema operativo anfitrión convencional (Windows, macOS, Linux). Las solicitudes de hardware de las máquinas virtuales deben atravesar el kernel del host.
   - *Ejemplos:* Oracle VM VirtualBox, VMware Workstation / Fusion.
   - *Rendimiento:* Menor rendimiento debido a la doble capa de abstracción y cambio de contexto entre el host y el hipervisor; ideal para laboratorios y desarrollo.

#### B. Métodos de Implementación de Virtualización
- **Atrapar y Emular (Trap-and-Emulate):** Basado en los requisitos clásicos de Popek y Goldberg (1974). Las instrucciones sensibles ejecutadas por el sistema operativo invitado en modo no privilegiado generan una excepción (*trap*), que es interceptada por el hipervisor para emular su comportamiento esperado.
- **Traducción Binaria (Binary Translation):** Popularizada por VMware en 1999 para la arquitectura x86 original (que presentaba 17 instrucciones sensibles no virtualizables según Popek-Goldberg). El hipervisor analiza y reescribe dinámicamente en tiempo de ejecución las secuencias de código problemáticas del kernel invitado.
- **Paravirtualización:** El núcleo del sistema operativo invitado es modificado intencionalmente para que no ejecute instrucciones privilegiadas directas, sino llamadas especiales al hipervisor (*hypercalls*), reduciendo drásticamente la sobrecarga de emulación (ej. controladores VirtIO).
- **Virtualización Asistida por Hardware:** Introducida por Intel (VT-x, 2005) y AMD (AMD-V, 2006). Añade un nuevo nivel de privilegio en la CPU: modo *root* (hipervisor) y modo *non-root* (invitados). Permite que las instrucciones sensibles pasen directamente al hardware sin necesidad de traducción binaria.

#### C. ¿Por qué `grep -cE 'vmx|svm' /proc/cpuinfo` difiere entre la VM y el Host?
Al ejecutar este comando:
- En la **máquina anfitriona (host)**, el valor suele ser mayor a 0 (ej. 8, 16 o 32 núcleos virtuales), indicando que el procesador físico posee soporte para Intel VT-x (`vmx`) o AMD-V (`svm`).
- En la **máquina virtual invitada (guest)**, el resultado suele ser **0**, a menos que se haya habilitado explícitamente la **Virtualización Anidada (Nested Virtualization)** en la configuración de la VM.
- *Razón técnica:* Por defecto, el hipervisor intercepta la instrucción `CPUID` mediante técnicas de enmascaramiento de características (*feature masking*) y oculta los bits de capacidades de virtualización (bit 5 de ECX en hoja 1 para VT-x, o bit 2 de ECX en hoja 0x80000001 para AMD-V). Esto se hace por estabilidad y seguridad, evitando que un kernel invitado intente inicializar las estructuras de control de máquina virtual (VMCS en Intel o VMCB en AMD) y competir con el hipervisor anfitrión por el control del procesador.

#### D. Diferencias Fundamentales: Máquinas Virtuales vs Contenedores
| Criterio | Máquina Virtual (VM) | Contenedor (Docker / Podman) |
| :--- | :--- | :--- |
| **Arquitectura** | Cada VM incluye una copia completa de un sistema operativo invitado, sus controladores y su propio kernel virtualizado. | Comparten el mismo kernel del sistema operativo anfitrión. |
| **Nivel de Aislamiento** | Aislamiento a nivel de hardware garantizado por el hipervisor y anillos de CPU (*strong isolation*). | Aislamiento a nivel de espacio de usuario (*user space*) mediante primitivas del kernel: Namespaces y Cgroups. |
| **Consumo de Memoria** | Alto: cada VM reserva varios gigabytes de RAM fija para su propio kernel y servicios. | Mínimo: consumen únicamente la memoria que sus procesos individuales requieren en cada instante. |
| **Tiempo de Arranque** | Minutos o decenas de segundos (inicialización completa de BIOS/UEFI, kernel y servicios). | Milisegundos o segundos (arranca como un proceso ordinario del kernel anfitrión). |
| **Sobrecarga de E/S** | Mayor: las llamadas al sistema del invitado deben atravesar emuladores de disco y red del hipervisor. | Casi nativa: acceso directo al subsistema de E/S del kernel host. |

---

### 3. Comparativa: Instantánea (Snapshot de VM) vs Partida Guardada (`partida.sav`)

| Dimensión | Instantánea de la Máquina Virtual (Snapshot) | Archivo de Partida Guardada (`partida_<usuario>.sav`) |
| :--- | :--- | :--- |
| **Capa de Abstracción** | **Nivel de Sistema Completo / Hardware:** Capa del Hipervisor (Capa inferior al SO). | **Nivel de Aplicación / Espacio de Usuario:** Capa 3 de MiniOS. |
| **Estado que Preserva** | Estado global del hardware virtualizado: páginas de RAM física asignadas, registros de CPU (EAX, EBX, EIP/RIP), punteros de pila, descriptores de dispositivos virtuales, buffers de red y estado diferencial del disco virtual (vmdk/vdi/vhdx). | Únicamente las variables de estado lógicas del videojuego: coordenadas de la serpiente `Punto cuerpo[MAX_COLA]`, posición de la comida, puntaje acumulado, nivel y dirección de avance. |
| **Independencia del SO** | Es totalmente ciega al contenido semántico del SO; restaura exactamente el estado binario del sistema completo, permitiendo revertir fallos del kernel o borrados de particiones completas. | Depende del sistema de archivos de MiniOS; si se destruye el disco o se borra la carpeta `datos/`, la partida se pierde salvo que el disco espejo o un snapshot del hipervisor la respalde. |
| **Tamaño de Persistencia** | Cientos de Megabytes o Gigabytes. | Exactamente **1664 bytes** (tamaño en disco del `struct Partida`). |
| **Tiempo de Restauración** | Varios segundos para volcar memoria diferencial y reconfigurar dispositivos virtuales. | Menos de 1 milisegundo mediante lectura secuencial con `read()`. |

> **Experimento Práctico:** Al tomar un Snapshot de la VM en VirtualBox/VMware/WSL2, eliminar completamente la carpeta `datos/` de MiniOS y restaurar el Snapshot, la máquina virtual vuelve al instante exacto previo al borrado, recuperando tanto el disco espejo como las partidas y la bitácora.

---

## 🛠️ Mejora 9: Mejoras Finales del Sistema Operativo

### 9.1 Autoguardado Concurrente con Mutex (Tema: Sincronización)
- **Implementación:** Durante la ejecución del videojuego Snake en `src/juego.c`, se crea un hilo POSIX concurrente (`hilo_autoguardado`) mediante `pthread_create()`.
- **Comportamiento Concurrente:** El hilo despierta periódicamente cada 15 segundos. Para leer el estado de la partida compartida (`static Partida partida;`), adquiere el cerrojo de exclusión mutua `pthread_mutex_lock(&mutex_partida)`. Copia atómicamente la estructura completa a una variable local `Partida copia;` y libera de inmediato el cerrojo con `pthread_mutex_unlock(&mutex_partida)`.
- **E/S Fuera de la Sección Crítica:** La llamada a `guardar_partida(&copia)` (que invoca llamadas al sistema costosas como `open`, `write` y `fsync`) se ejecuta **estrictamente fuera del mutex**. Si se ejecutara dentro del cerrojo, el hilo principal de renderizado del juego (que dibuja a 180 ms por cuadro) se congelaría perceptiblemente durante las operaciones de escritura en disco, arruinando la fluidez del juego.
- **¿Qué pasaría sin el Mutex? (Condición de Carrera):**
  Sin el cerrojo de exclusión mutua, ocurriría una condición de carrera (*race condition*) del tipo lectura inconsistente (*torn read*). Si el hilo de autoguardado lee la estructura mientras el hilo del juego está a mitad de actualizar el arreglo de coordenadas `cuerpo[]` (por ejemplo, habiendo desplazado la cabeza a la posición $(x_1, y_1)$ pero manteniendo los segmentos intermedios en posiciones viejas), la partida guardada contendría un estado espacialmente corrupto o imposible (serpiente fragmentada o teletransportada). El mutex garantiza consistencia secuencial y atomicidad en cada captura del estado.
- **Prueba de Resistencia (`kill -9`):** Al matar el proceso abruptamente con `SIGKILL`, el archivo `datos/partida_<usuario>.sav` permanece intacto en disco con el último estado guardado por el hilo concurrente. Al reiniciar MiniOS, la opción *Continuar Partida Guardada* reanuda la sesión en el punto exacto guardado.

---

### 9.2 Arreglo de Disco Espejo — RAID 1 (Tema: Memoria Secundaria y Redundancia)
- **Implementación:** El sistema de archivos en `src/fs.c` administra simultáneamente dos descriptores de archivo: `disco_fd` (`datos/minios.disk`) y `espejo_fd` (`datos/minios_espejo.disk`).
- **Doble Escritura Síncrona:** La función de bajo nivel `escribir_en_ambos(pos, datos, n)` canaliza cada modificación de ranuras (`escribir_registro`) y superbloque (`guardar_superbloque`) hacia ambos archivos, forzando la sincronización física en disco mediante llamadas directas a `fsync()`.
- **Detección de Apagado Sucio:** El superbloque mantiene el flag `sb.limpio`. Al montar el disco se marca como `limpio = 0`. Solo al invocar el desmontaje ordenado (`fs_desmontar`) se escribe `limpio = 1`. Si el sistema cae abruptamente por fallo de energía o `kill -9`, el siguiente arranque detecta el estado y alerta al usuario:
  `⚠️ ADVERTENCIA: El disco no se desmontó correctamente en la última sesión (apagado sucio detectado).`
- **Algoritmo de Reconstrucción Automática (Self-Healing):**
  Al montar el almacenamiento (`fs_montar`):
  1. Si `minios.disk` está ausente o su número mágico (`0x4D494E49`) está dañado, pero `minios_espejo.disk` está íntegro, el sistema copia bloque a bloque (4096 bytes) desde el espejo hacia el disco principal, recuperando el 100 % de los datos y emitiendo:
     `⚠️ [RAID 1] Disco principal dañado o ausente. Reconstruyendo desde el espejo...`
     `✅ [RAID 1] Disco reconstruido desde el espejo.`
  2. Si el espejo es el dañado o borrado, la reconstrucción ocurre en sentido inverso (desde el principal al espejo).
  3. Cada evento de autoreconstrucción queda registrado formalmente en la bitácora mediante `auditar("RAID1_RECONSTR", ..., "OK")`.

---

### 9.3 Mejoras Opcionales Implementadas (Temas: Memoria Principal y Planificación)

MiniOS v3.0 integra dos mejoras adicionales para enriquecer la sustentación:

#### 1. Monitor de Memoria Principal (Memoria Virtual, Física y Marcos de Página)
En el menú **6. Gestión de Procesos** (Opción 6: *Ver Monitor de Memoria*), el sistema abre el archivo virtual del kernel `/proc/<pid>/status` de cualquier proceso en ejecución y extrae:
- **`VmSize` (Memoria Virtual Total):** Espacio de direcciones virtual reservado por el proceso.
- **`VmRSS` (Resident Set Size):** Memoria física real asignada en la memoria RAM principal.
- **Tamaño de Página del SO:** Obtenido en tiempo real mediante `sysconf(_SC_PAGESIZE)` (4096 bytes en arquitectura x86_64).
- **Cálculo de Páginas/Marcos Ocupados:**
  $$\text{Páginas Físicas en RAM} = \frac{\text{VmRSS (KB)} \times 1024}{\text{Tamaño de Página (Bytes)}}$$
  Permite explicar conceptos fundamentales de la materia: paginación por demanda, tablas de páginas y fragmentación interna.

#### 2. Planificación de CPU y Prioridades de Procesos (`nice`)
En el menú de procesos (Opción 7: *Cambiar Prioridad*):
- Permite consultar la prioridad estática del proceso mediante `getpriority(PRIO_PROCESS, pid)`.
- Permite ajustar dinámicamente el valor de cortesía (*niceness*) entre `-20` (máxima prioridad) y `19` (mínima prioridad) mediante la llamada al sistema `setpriority()`.
- La lista de procesos (`proc_listar`) muestra la columna `Nice` en tiempo real.
- Todo cambio de prioridad queda registrado en la bitácora (`auditar("CAMBIO_PRIO", ...)`) y respeta el principio de confinamiento de propiedad (`seg_es_propietario`).

---

## 🧪 Prueba de Fuego Final de MiniOS v3.0 (Checklist de Sustentación)

| Criterio a Evaluar | Estado | Verificación en Vivo |
| :--- | :---: | :--- |
| **Compilación Limpia** | ✅ | `make clean && make` compila sin advertencias con `-Wall -Wextra -pthread`. |
| **Login Obligatorio y Clave Oculta** | ✅ | Menú bloqueado hasta autenticar; `termios` apaga el eco; 3 claves erróneas bloquean la cuenta. |
| **Confinamiento de Invitado** | ✅ | Rol `invitado` recibe `ACCESO DENEGADO` en opciones administrativas y el sistema no se cierra. |
| **Alerta Forense por Denegaciones** | ✅ | Al acumular 5 denegaciones en la sesión, MiniOS dispara advertencia en pantalla y audita la alerta. |
| **RAID 1 y Reconstrucción** | ✅ | Se borra `datos/minios.disk`, se arranca MiniOS y se reconstruye automáticamente desde el espejo. |
| **Detección de Apagado Sucio** | ✅ | Si se mata el proceso en caliente, el superbloque advierte apagado incorrecto en el siguiente inicio. |
| **Control de Procesos BSD** | ✅ | `tetris-bsd` se suspende con `Ctrl+Z`, se reanuda con `SIGCONT` y termina sin dejar zombis (`waitpid`). |
| **Autoguardado Concurrente con Mutex**| ✅ | El hilo en segundo plano salva la partida cada 15s; resiste `kill -9` y permite reanudar el juego. |
| **Bitácora Integral** | ✅ | Solo `admin` puede verla; resume accesos denegados, fallos de login y el usuario más denegado. |
| **Módulo de Virtualización** | ✅ | CPUID identifica CPU, hipervisor, VT-x/AMD-V, EPT/NPT, DMI y resume en una frase conclusiva. |
| **Monitor de Memoria y Prioridades** | ✅ | Despliega VmSize, VmRSS, marcos de página y permite alterar valores de `nice`. |
| **Robustez ante Entradas Inválidas** | ✅ | Entradas de texto en prompts numéricos no rompen el ciclo; solo `admin` puede apagar el sistema. |
