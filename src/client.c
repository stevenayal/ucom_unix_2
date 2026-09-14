#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT "5555"
#define BUFFER_SIZE 4096
#define MAX_LINE 1024

static int send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int send_line(int fd, const char *text) {
    if (send_all(fd, text, strlen(text)) < 0) return -1;
    return send_all(fd, "\n", 1);
}

static ssize_t recv_line(int fd, char *buffer, size_t size) {
    if (size == 0) return -1;
    size_t used = 0;
    while (used + 1 < size) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) {
            if (used == 0) return 0;
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') break;
        if (c != '\r') buffer[used++] = c;
    }
    buffer[used] = '\0';
    return (ssize_t)used;
}

static int connect_to_server(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *it;
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port, &hints, &results);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    for (it = results; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;

        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }

    freeaddrinfo(results);
    return fd;
}

static int perform_authentication(int fd) {
    char line[MAX_LINE];
    char user[MAX_LINE];
    char pass[MAX_LINE];

    if (recv_line(fd, line, sizeof(line)) <= 0 || strcmp(line, "USER") != 0) {
        fprintf(stderr, "Protocolo invalido: se esperaba USER\n");
        return -1;
    }

    printf("Usuario: ");
    fflush(stdout);
    if (!fgets(user, sizeof(user), stdin)) return -1;
    user[strcspn(user, "\r\n")] = '\0';
    if (send_line(fd, user) < 0) return -1;

    if (recv_line(fd, line, sizeof(line)) <= 0 || strcmp(line, "PASS") != 0) {
        fprintf(stderr, "Protocolo invalido: se esperaba PASS\n");
        return -1;
    }

    printf("Contrasena: ");
    fflush(stdout);
    if (!fgets(pass, sizeof(pass), stdin)) return -1;
    pass[strcspn(pass, "\r\n")] = '\0';
    if (send_line(fd, pass) < 0) return -1;

    if (recv_line(fd, line, sizeof(line)) <= 0) return -1;
    if (strcmp(line, "AUTH_OK") != 0) {
        fprintf(stderr, "Autenticacion rechazada.\n");
        return 0;
    }

    if (recv_line(fd, line, sizeof(line)) <= 0 || strcmp(line, "READY") != 0) {
        fprintf(stderr, "El servidor no quedo listo.\n");
        return -1;
    }

    puts("Autenticacion correcta. Escribi comandos Unix o 'exit'.");
    return 1;
}

static int print_command_response(int fd) {
    char line[BUFFER_SIZE];

    for (;;) {
        ssize_t n = recv_line(fd, line, sizeof(line));
        if (n <= 0) return -1;

        if (strcmp(line, "__END__") == 0) break;
        if (strncmp(line, "__STATUS__:", 11) == 0) {
            printf("[codigo de salida %s]\n", line + 11);
            continue;
        }
        if (strncmp(line, "__SIGNAL__:", 11) == 0) {
            printf("[terminado por senal %s]\n", line + 11);
            continue;
        }

        puts(line);
    }

    return 0;
}

int main(int argc, char **argv) {
    const char *host = DEFAULT_HOST;
    const char *port = DEFAULT_PORT;

    if (argc > 3) {
        fprintf(stderr, "Uso: %s [host] [puerto]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc >= 2) host = argv[1];
    if (argc == 3) port = argv[2];

    int fd = connect_to_server(host, port);
    if (fd < 0) {
        fprintf(stderr, "No se pudo conectar a %s:%s\n", host, port);
        return EXIT_FAILURE;
    }

    int auth = perform_authentication(fd);
    if (auth != 1) {
        close(fd);
        return auth == 0 ? EXIT_FAILURE : EXIT_FAILURE;
    }

    char command[MAX_LINE];
    for (;;) {
        printf("ucom$ ");
        fflush(stdout);

        if (!fgets(command, sizeof(command), stdin)) {
            send_line(fd, "exit");
            break;
        }
        command[strcspn(command, "\r\n")] = '\0';

        if (send_line(fd, command) < 0) {
            perror("send");
            break;
        }

        if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
            char bye[MAX_LINE];
            if (recv_line(fd, bye, sizeof(bye)) > 0) puts(bye);
            break;
        }

        if (print_command_response(fd) < 0) {
            fprintf(stderr, "Conexion cerrada por el servidor.\n");
            break;
        }
    }

    close(fd);
    return EXIT_SUCCESS;
}
