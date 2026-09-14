# UCOM Unix 2 - TP1 Emulacion de Terminal

Implementacion en C de una terminal remota cliente-servidor para Unix/Linux.

## Que implementa

- Comunicacion TCP con sockets.
- Servidor multiproceso: `fork()` por cliente.
- Ejecucion remota de comandos Unix.
- `pipe()` para capturar la salida del proceso ejecutado.
- `dup2()` para redirigir `stdout` y `stderr` al pipe.
- `execl()` para reemplazar el proceso por `/bin/sh -c`.
- Manejo de `SIGCHLD` con `waitpid(..., WNOHANG)` para evitar zombies.
- Manejo de `SIGINT`/`SIGTERM` para cierre ordenado.
- Autenticacion simple usuario/contrasena.
- Multiples clientes concurrentes.
- Prueba de integracion automatizada.

## Entorno

El repositorio incluye un Dev Container para GitHub Codespaces con Ubuntu 24.04, GCC, GDB, Make, Valgrind y Strace.

## Compilar

```bash
make
```

Genera:

```text
./server
./client
```

## Ejecutar

### Terminal 1 - servidor

```bash
./server
```

Por defecto escucha en el puerto `5555`.

Tambien se puede elegir otro puerto:

```bash
./server 6000
```

Credenciales por defecto:

```text
usuario: ucom
contrasena: unix
```

Se pueden cambiar sin modificar el codigo:

```bash
TP_USER=steven TP_PASS=clave ./server
```

### Terminal 2 - cliente

```bash
./client 127.0.0.1 5555
```

Luego ingresar usuario y contrasena.

Ejemplo:

```text
Usuario: ucom
Contrasena: unix
Autenticacion correcta. Escribi comandos Unix o 'exit'.
ucom$ pwd
/workspaces/ucom_unix_2
[codigo de salida 0]
ucom$ ls -la
...
ucom$ uname -a
...
ucom$ exit
BYE
```

## Prueba automatizada

```bash
make test
```

La prueba levanta un servidor temporal, conecta el cliente, se autentica, ejecuta comandos y comprueba sus respuestas.

## Herramientas de analisis

Depurar el servidor:

```bash
gdb ./server
```

Revisar memoria:

```bash
valgrind --leak-check=full ./server
```

Observar llamadas al sistema:

```bash
strace -f ./server
```

El parametro `-f` es importante porque el servidor usa `fork()`.

## Estructura

```text
.devcontainer/
  devcontainer.json
.github/
  workflows/
    build.yml
src/
  client.c
  server.c
  main.c
tests/
  integration_test.sh
Makefile
README.md
.gitignore
```

`src/main.c` queda solamente como archivo inicial historico; los programas del TP son `server.c` y `client.c`.

## GitHub Codespaces

En GitHub abrir:

`Code -> Codespaces -> Create codespace on main`

Cuando termine de preparar el entorno:

```bash
make
make test
```

## Flujo de procesos

Servidor principal:

```text
socket -> bind -> listen -> accept
                    |
                    +-> fork() -> proceso cliente
                                    |
                                    +-> autenticar
                                    +-> recibir comando
                                    +-> pipe()
                                    +-> fork()
                                         |
                                         +-> dup2(stdout/stderr)
                                         +-> execl(/bin/sh, sh -c comando)
```

El proceso padre mantiene el socket de escucha. Cada conexion es atendida por un hijo independiente, permitiendo clientes concurrentes.
