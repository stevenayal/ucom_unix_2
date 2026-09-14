#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT "5555"
#define BUFFER_SIZE 4096
#define MAX_LINE 1024

static int send_all(int fd, const void *data, size_t len) {
    const char *p = data;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        p += n; len -= (size_t)n;
    }
    return 0;
}

static ssize_t recv_line(int fd, char *buf, size_t size) {
    size_t used = 0;
    while (used + 1 < size) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0) return n;
        if (c == '\n') break;
        if (c != '\r') buf[used++] = c;
    }
    buf[used] = '\0';
    return (ssize_t)used;
}

static int connect_server(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res = NULL, *it;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;
    int fd = -1;
    for (it = res; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd >= 0 && connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        if (fd >= 0) close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int auth(int fd) {
    char line[MAX_LINE], input[MAX_LINE];
    if (recv_line(fd, line, sizeof line) <= 0 || strcmp(line, "USER")) return -1;
    printf("Usuario: "); fflush(stdout);
    if (!fgets(input, sizeof input, stdin)) return -1;
    if (send_all(fd, input, strlen(input)) < 0) return -1;

    if (recv_line(fd, line, sizeof line) <= 0 || strcmp(line, "PASS")) return -1;
    printf("Contrasena: "); fflush(stdout);
    if (!fgets(input, sizeof input, stdin)) return -1;
    if (send_all(fd, input, strlen(input)) < 0) return -1;

    if (recv_line(fd, line, sizeof line) <= 0 || strcmp(line, "AUTH_OK")) return 0;
    if (recv_line(fd, line, sizeof line) <= 0 || strcmp(line, "READY")) return -1;
    puts("Autenticacion correcta. Terminal remota iniciada.");
    return 1;
}

static int interactive_loop(int fd) {
    struct pollfd fds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = fd, .events = POLLIN }
    };
    char buf[BUFFER_SIZE];

    for (;;) {
        int rc = poll(fds, 2, -1);
        if (rc < 0) { if (errno == EINTR) continue; return -1; }

        if (fds[0].revents & POLLIN) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n <= 0) { shutdown(fd, SHUT_WR); fds[0].fd = -1; }
            else if (send_all(fd, buf, (size_t)n) < 0) return -1;
        }
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = recv(fd, buf, sizeof buf, 0);
            if (n <= 0) break;
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = write(STDOUT_FILENO, buf + off, (size_t)(n - off));
                if (w < 0) { if (errno == EINTR) continue; return -1; }
                off += w;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *host = DEFAULT_HOST, *port = DEFAULT_PORT;
    if (argc > 3) { fprintf(stderr, "Uso: %s [host] [puerto]\n", argv[0]); return EXIT_FAILURE; }
    if (argc >= 2) host = argv[1];
    if (argc == 3) port = argv[2];

    int fd = connect_server(host, port);
    if (fd < 0) { fprintf(stderr, "No se pudo conectar a %s:%s\n", host, port); return EXIT_FAILURE; }
    int ok = auth(fd);
    if (ok != 1) { fprintf(stderr, "Autenticacion/protocolo rechazado.\n"); close(fd); return EXIT_FAILURE; }
    interactive_loop(fd);
    close(fd);
    return EXIT_SUCCESS;
}
