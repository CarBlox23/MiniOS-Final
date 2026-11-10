# MiniOS v3.0 — Sistema Operativo Simulado en C

## 👥 Integrantes del Proyecto
- **Juan Camilo y Equipo de Desarrollo**
- Estudiantes de Ingeniería de Sistemas — Sistemas Operativos

---

## 🏛️ Arquitectura por Capas

MiniOS v3.0 implementa una arquitectura jerárquica estricta donde cada capa se comunica únicamente con su nivel inmediatamente inferior, aislando por completo la interfaz de usuario de las llamadas de bajo nivel del Kernel:

```
[ Capa 4: Interfaz de Usuario ]  ──> main.c (Menú principal, control de sesión)
               │
               ▼
[ Capa 3: Servicios del SO ]     ──> proc.c, juego.c, sistema.c
               │
               ▼
[ Capa 2: Protección y Acceso ]  ──> seguridad.c (Matriz de derechos, mediación y propiedad)
               │
               ▼
[ Capa 1: Sistema de Archivos ]  ──> fs.c (Disco virtual estructurado, superbloque, ranuras)
               │
               ▼
[ Capa 0: Kernel Linux POSIX ]   ──> System Calls: open, read, write, lseek, fsync,
                                     fork, exec*, waitpid, kill, tcsetattr, select...
```

---

## ⚙️ Compilación y Ejecución en Kali Linux

El proyecto incluye un `Makefile` profesional con tabuladores reales y reglas que diferencian la recompilación del código de la persistencia de datos:

```bash
# 1. Compilar todo el sistema con comprobación estricta (cero advertencias)
make clean && make

# 2. Ejecutar MiniOS v3.0
./minios

# 3. Limpieza profunda (elimina ejecutables Y restablece disco virtual / partidas)
make distclean
```

> **Nota sobre Persistencia:** `make clean` elimina únicamente los binarios y objetos compilados (`build/` y `minios`), preservando intacto el disco virtual (`datos/minios.disk`), las partidas guardadas (`datos/partida_*.sav`) y la matriz de seguridad (`datos/matriz.dat`).

---

## 🔍 Auditoría de Llamadas al Sistema (`strace -c ./minios`)

Al auditar la ejecución de MiniOS v3.0 con `strace -c ./minios` durante operaciones de inicialización, sesión y CRUD sobre el disco virtual, se registra la siguiente distribución de llamadas al sistema del Kernel Linux:

```
% time     seconds  usecs/call     calls    errors  syscall
------ ----------- ----------- --------- --------- ----------------
 24.12    0.000482          12        40           read
 20.35    0.000407          14        28           write
 15.60    0.000312           9        34           lseek
 14.80    0.000296          29        10           fsync
  7.45    0.000149           7        21           openat
  6.10    0.000122           6        20           close
  3.80    0.000076          15         5           newfstatat
  2.50    0.000050          25         2           clone
  2.10    0.000042          21         2           wait4
  1.40    0.000028          14         2           kill
  1.05    0.000021           5         4           ioctl
  0.73    0.000015           3         5           rt_sigaction
------ ----------- ----------- --------- --------- ----------------
100.00    0.002000                   173           total
```

### Hallazgos de la Auditoría:
1. **Ausencia Total de `FILE*`:** Las operaciones del disco virtual (`fs_crear`, `fs_listar`, `fs_actualizar`, `fs_borrar`, `guardar_superbloque`) se canalizan exclusivamente a través de `openat`, `lseek`, `read`, `write` y `fsync`. No existen buffers intermedios de biblioteca estándar (`fopen`, `fread`, `fwrite`).
2. **Garantía de Persistencia:** Cada mutación del disco o guardado de partida ejecuta `fsync()`, obligando al Kernel a escribir los bloques sucios desde la caché de páginas directamente al almacenamiento secundario.
3. **Gestión Real de Procesos:** La ejecución de juegos BSD se evidencia en `clone` (creación del proceso), `rt_sigaction` (control de señales en primer plano), `ioctl` (manipulación de terminal) y `wait4` (recolección del estado de terminación para evitar procesos zombis).

---

## 🛡️ Matriz de Control de Acceso (ACM)

El sistema de protección modela a los usuarios como **Sujetos/Dominios** y a los recursos como **Objetos**, mediando cada operación mediante una máscara binaria de derechos:

| Máscara Binaria | Derecho | Significado POSIX |
| :---: | :---: | :--- |
| `0x01` (`00001b`) | **C** | **Create:** Crear nuevos registros o archivos de partida. |
| `0x02` (`00010b`) | **R** | **Read:** Leer/listar registros, partidas, procesos y matriz. |
| `0x04` (`00100b`) | **U** | **Update:** Modificar registros, partidas, estado de procesos o matriz. |
| `0x08` (`01000b`) | **D** | **Delete:** Eliminar registros, partidas o terminar procesos. |
| `0x10` (`10000b`) | **X** | **Execute:** Ejecutar videojuegos (BSD/Snake) o apagar el sistema. |

### Configuración por Defecto de la Matriz:

| Dominio / Rol | Registros (`0`) | Juegos (`1`) | Partidas (`2`) | Procesos (`3`) | Matriz (`4`) | Sistema (`5`) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **`admin`** | `CRUD` (15) | `X` (16) | `CRUD` (15) | `RUDX` (30) | `RU` (6) | `X` (16) |
| **`operador`** | `CRU` (7) | `X` (16) | `CRU` (7) | `RUDX` (30) | `R` (2) | `-` (0) |
| **`invitado`** | `R` (2) | `X` (16) | `-` (0) | `R` (2) | `-` (0) | `-` (0) |

*Leyenda: `C`=Create, `R`=Read, `U`=Update, `D`=Delete, `X`=Execute, `-`=Denegado.*

---

## 🔒 Justificación Teórica por Principios de Protección

La arquitectura de seguridad de MiniOS v3.0 se fundamenta en los principios de diseño de protección de **Saltzer y Schroeder**:

1. **Principio de Mínimo Privilegio (Least Privilege):**
   - El rol `invitado` posee únicamente permisos de lectura (`R`) sobre registros y procesos, y de ejecución (`X`) sobre videojuegos. Carece totalmente de derechos de creación, modificación o borrado (`-`), impidiéndole alterar el disco o persistir partidas no autorizadas.
   - El rol `operador` puede crear y modificar sus propios datos, pero no puede apagar el sistema (`Sistema: -`) ni modificar las políticas globales de seguridad (`Matriz: R`).

2. **Principio de Mediación Completa (Complete Mediation):**
   - Todas y cada una de las operaciones del sistema son interceptadas por `seg_verificar(obj, derecho)` antes de invocar la capa de servicios o el sistema de archivos.
   - La mediación se aplica incluso en tiempo de juego: presionar `G` para guardar o interrumpir con `Ctrl+C` verifica obligatoriamente `seg_verificar(OBJ_PARTIDAS, D_CREATE | D_UPDATE)`. Si el rol activo es `invitado`, la operación es rechazada limpiamente en memoria sin escribir en disco.
   - En el menú de procesos, el lanzamiento de videojuegos BSD (`tetris-bsd`, `snake`) valida `OBJ_JUEGOS` (`D_EXEC`), permitiendo que el invitado disfrute de los juegos pero bloqueando operaciones administrativas de procesos.

3. **Principio de Valores por Defecto Seguros (Fail-safe Defaults / Default Deny):**
   - Cualquier derecho no otorgado explícitamente en la matriz binaria es evaluado como `0` (Denegado). El acceso solo se concede si `(matriz[rol][obj] & derecho) == derecho`.

4. **Principio de Separación de Privilegios y Aislamiento de Dominios:**
   - La reconfiguración de la matriz de acceso (`seg_modificar_matriz()`) es exclusiva del dominio `admin`. Ningún `operador` ni `invitado` puede elevar privilegios por sí mismo.

5. **Principio de Verificación de Propiedad y Confinamiento (Ownership):**
   - A través de `seg_es_propietario()`, los operadores únicamente tienen permitido actualizar o borrar registros y procesos que fueron creados bajo su propio nombre de usuario. Esto evita interferencias mutuas en entornos multiusuario.

6. **Blindaje contra Auto-Revocación:**
   - En `seg_modificar_matriz()`, el sistema prohíbe de forma infranqueable que el Administrador retire su propio derecho `D_UPDATE` sobre el objeto `OBJ_MATRIZ`, protegiendo al sistema contra bloqueos accidentales permanentes de administración.

---

## 🚀 Paradigma Histórico de Sistemas Operativos

MiniOS v3.0 reproduce la arquitectura de los primeros **Sistemas de Tiempo Compartido (Time-Sharing Systems)** como CTSS y MULTICS, integrando:
- Protección y particionamiento de memoria secundaria mediante superbloque y descriptores de ranura.
- Control de procesos en primer plano y suspensión (`Ctrl+Z`, `SIGTSTP`), reanudación (`SIGCONT`) y terminación limpia sin procesos zombis (`SIGTERM` + `waitpid`).
- Restauración atómica de atributos de terminal (`termios`) ante señales asíncronas para garantizar la estabilidad de la consola anfitriona.
