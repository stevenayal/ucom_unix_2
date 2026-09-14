#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_PORT 5555
#define BACKLOG 10
#define BUFFER_SIZE 4096
#define MAX_LINE 1024

static volatile sig_atomic_t stop_server = 0;
static int listen_fd = -1;

static void handle_sigint(int sig) {
    (void)sig;
    stop_server = 1;
    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }
}

static void reap_children(int sig) {
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    errno = saved_errno;
}

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

static int send_text(int fd, const char *text) {
    return send_all(fd, text, strlen(text));
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

static int authenticate_client(int client_fd) {
    const char *expected_user = getenv("TP_USER");
    const char *expected_pass = getenv("TP_PASS");
    if (!expected_user) expected_user = "ucom";
    if (!expected_pass) expected_pass = "unix";

    char user[MAX_LINE];
    char pass[MAX_LINE];

    if (send_text(client_fd, "USER\n") < 0) return -1;
    if (recv_line(client_fd, user, sizeof(user)) <= 0) return -1;

    if (send_text(client_fd, "PASS\n") < 0) return -1;
    if (recv_line(client_fd, pass, sizeof(pass)) <= 0) return -1;

    if (strcmp(user, expected_user) != 0 || strcmp(pass, expected_pass) != 0) {
        send_text(client_fd, "AUTH_FAIL\n");
        return 0;
    }

    if (send_text(client_fd, "AUTH_OK\n") < 0) return -1;
    return 1;
}

static int execute_command(int client_fd, const char *command) {
    int output_pipe[2];
    if (pipe(output_pipe) < 0) {
        return send_text(client_fd, "ERROR: pipe() fallo\n__END__\n");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        return send_text(client_fd, "ERROR: fork() fallo\n__END__\n");
    }

    if (pid == 0) {
        close(output_pipe[0]);

        if (dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(output_pipe[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(output_pipe[1]);

        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        perror("execl");
        _exit(127);
    }

    close(output_pipe[1]);

    char buffer[BUFFER_SIZE];
    for (;;) {
        ssize_t n = read(output_pipe[0], buffer, sizeof(buffer));
        if (n > 0) {
            if (send_all(client_fd, buffer, (size_t)n) < 0) {
                close(output_pipe[0]);
                waitpid(pid, NULL, 0);
                return -1;
            }
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    close(output_pipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }

    char status_line[64];
    if (WIFEXITED(status)) {
        snprintf(status_line, sizeof(status_line), "__STATUS__:%d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        snprintf(status_line, sizeof(status_line), "__SIGNAL__:%d\n", WTERMSIG(status));
    } else {
        snprintf(status_line, sizeof(status_line), "__STATUS__:-1\n");
    }

    if (send_text(client_fd, status_line) < 0) return -1;
    return send_text(client_fd, "__END__\n");
}

static void serve_client(int client_fd) {
    int auth = authenticate_client(client_fd);
    if (auth != 1) return;

    if (send_text(client_fd, "READY\n") < 0) return;

    char command[MAX_LINE];
    while (recv_line(client_fd, command, sizeof(command)) > 0) {
        if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
            send_text(client_fd, "BYE\n");
            break;
        }

        if (command[0] == '\0') {
            if (send_text(client_fd, "__END__\n") < 0) break;
            continue;
        }

        if (execute_command(client_fd, command) < 0) break;
    }
}

static int parse_port(const char *value) {
    char *end = NULL;
    long port = strtol(value, &end, 10);
    if (!value[0] || (end && *end != '\0') || port < 1 || port > 65535) {
        return -1;
    }
    return (int)port;
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    if (argc > 2) {
        fprintf(stderr, "Uso: %s [puerto]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        port = parse_port(argv[1]);
        if (port < 0) {
            fprintf(stderr, "Puerto invalido: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    }

    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = reap_children;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa_chld, NULL) < 0) {
        perror("sigaction SIGCHLD");
        return EXIT_FAILURE;
    }

    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    if (sigaction(SIGINT, &sa_int, NULL) < 0 || sigaction(SIGTERM, &sa_int, NULL) < 0) {
        perror("sigaction shutdown");
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int reuse = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("Servidor escuchando en 0.0.0.0:%d\n", port);
    printf("Credenciales por defecto: ucom / unix\n");
    printf("Ctrl+C para apagar ordenadamente.\n");
    fflush(stdout);

    while (!stop_server) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (stop_server || errno == EBADF) break;
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork cliente");
            close(client_fd);
            continue;
        }

        if (pid == 0) {
            if (listen_fd >= 0) close(listen_fd);
            serve_client(client_fd);
            close(client_fd);
            _exit(EXIT_SUCCESS);
        }

        close(client_fd);
    }

    if (listen_fd >= 0) close(listen_fd);

    while (waitpid(-1, NULL, 0) > 0) {
    }

    puts("Servidor finalizado.");
    return EXIT_SUCCESS;
}
