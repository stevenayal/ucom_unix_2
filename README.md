# UCOM Unix 2 - TP1 Emulacion de Terminal

Proyecto base en C preparado para desarrollar el trabajo practico de emulacion de terminal.

## Entorno

El repositorio incluye un Dev Container para GitHub Codespaces con:

- Ubuntu 24.04
- GCC
- GDB
- Make
- Valgrind
- Strace

## Uso

Compilar:

```bash
make
```

Ejecutar:

```bash
make run
```

Depurar:

```bash
gdb ./terminal
```

Revisar memoria:

```bash
valgrind ./terminal
```

Analizar llamadas al sistema:

```bash
strace ./terminal
```

## Estructura

```text
.devcontainer/devcontainer.json
.github/workflows/build.yml
src/main.c
Makefile
.gitignore
README.md
```

## GitHub Codespaces

En GitHub, abrir el repositorio y seleccionar:

`Code -> Codespaces -> Create codespace on main`

El entorno se configurara automaticamente.

## CI

Cada push y pull request ejecuta `make` en Ubuntu mediante GitHub Actions.
