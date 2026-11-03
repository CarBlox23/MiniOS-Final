# MiniOS v3.0 — Sistema Operativo Simulado en C

## Integrantes
- Estudiante de Ingeniería de Sistemas

## Compilación y Ejecución en Kali Linux

```bash
# Compilar todo el proyecto sin advertencias
make clean && make

# Ejecutar el sistema
./minios

# Para auditar llamadas al sistema durante Create y Read:
strace -c ./minios
```

## Diagrama de Capas
- **Capa 4 (Interfaz):** `main.c` (Menú interactivo y sesión).
- **Capa 3 (Servicios):** `proc.c`, `juego.c`, `sistema.c`.
- **Capa 2 (Protección):** `seguridad.c` (Matriz de derechos y propiedad).
- **Capa 1 (Sistema de Archivos):** `fs.c` (Disco virtual estructurado).
- **Capa 0 (Kernel Linux):** System calls directas (`open`, `read`, `write`, `lseek`, `fsync`, `fork`, `exec*`, `waitpid`).

## Auditoría strace -c
Al ejecutar `strace -c ./minios` y realizar operaciones CRUD se aprecian llamadas a `read`, `write`, `lseek` y `fsync`, confirmando la ausencia de funciones de alto nivel (`FILE*`).

## Paradigma Histórico
MiniOS v3.0 asemeja a los primeros **Sistemas de Tiempo Compartido (Time-Sharing Systems)** como CTSS y Multics, con separación de roles, protección de memoria secundaria y control de procesos en primer y segundo plano.
