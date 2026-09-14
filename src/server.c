#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
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

static void shutdown_handler(int sig) {
    (void)sig;
    stop_server = 1;
    if (listen_fd >= 0) close(listen_fd);
}

static void reap_children(int sig) {
    (void)sig;
    int saved = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
    errno = saved;
}

static int send_all(int fd, const void *data, size_t len) {
    const char *p = data;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        p += n;
        len -= (size_t)n;
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

static int authenticate(int fd) {
    const char *user_ok = getenv("TP_USER");
    const char *pass_ok = getenv("TP_PASS");
    char user[MAX_LINE], pass[MAX_LINE];
    if (!user_ok) user_ok = "ucom";
    if (!pass_ok) pass_ok = "unix";

    if (send_all(fd, "USER\n", 5) < 0 || recv_line(fd, user, sizeof user) <= 0) return -1;
    if (send_all(fd, "PASS\n", 5) < 0 || recv_line(fd, pass, sizeof pass) <= 0) return -1;
    if (strcmp(user, user_ok) || strcmp(pass, pass_ok)) {
        send_all(fd, "AUTH_FAIL\n", 10);
        return 0;
    }
    return send_all(fd, "AUTH_OK\n", 8) < 0 ? -1 : 1;
}

static void reset_child_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static void relay_shell(int client_fd) {
    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0) return;

    pid_t shell = fork();
    if (shell < 0) return;

    if (shell == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(client_fd);
        setenv("PS1", "ucom-remote:\\w$ ", 1);
        char *const argv[] = {"bash", "--noprofile", "--norc", "-i", NULL};
        extern char **environ;
        execve("/bin/bash", argv, environ);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    struct pollfd fds[2] = {
        { .fd = client_fd, .events = POLLIN },
        { .fd = out_pipe[0], .events = POLLIN }
    };
    char buf[BUFFER_SIZE];
    int running = 1;

    while (running) {
        int rc = poll(fds, 2, -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = recv(client_fd, buf, sizeof buf, 0);
            if (n <= 0) break;
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = write(in_pipe[1], buf + off, (size_t)(n - off));
                if (w < 0) { if (errno == EINTR) continue; running = 0; break; }
                off += w;
            }
        }
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = read(out_pipe[0], buf, sizeof buf);
            if (n <= 0) break;
            if (send_all(client_fd, buf, (size_t)n) < 0) break;
        }
    }

    close(in_pipe[1]);
    close(out_pipe[0]);
    kill(shell, SIGTERM);
    while (waitpid(shell, NULL, 0) < 0 && errno == EINTR) {}
}

static void serve_client(int client_fd) {
    if (authenticate(client_fd) != 1) return;
    if (send_all(client_fd, "READY\n", 6) < 0) return;
    relay_shell(client_fd);
}

static int parse_port(const char *s) {
    char *end = NULL;
    long p = strtol(s, &end, 10);
    return (!*s || *end || p < 1 || p > 65535) ? -1 : (int)p;
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    if (argc > 2 || (argc == 2 && (port = parse_port(argv[1])) < 0)) {
        fprintf(stderr, "Uso: %s [puerto]\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct sigaction chld = {0}, stop = {0};
    chld.sa_handler = reap_children;
    chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&chld.sa_mask);
    sigaction(SIGCHLD, &chld, NULL);
    stop.sa_handler = shutdown_handler;
    sigemptyset(&stop.sa_mask);
    sigaction(SIGINT, &stop, NULL);
    sigaction(SIGTERM, &stop, NULL);
    signal(SIGPIPE, SIG_IGN);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return EXIT_FAILURE; }
    int one = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(listen_fd, BACKLOG) < 0) {
        perror("bind/listen"); close(listen_fd); return EXIT_FAILURE;
    }

    printf("Servidor escuchando en 0.0.0.0:%d\n", port);
    fflush(stdout);

    while (!stop_server) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (stop_server || errno == EBADF) break;
            continue;
        }
        pid_t pid = fork();
        if (pid == 0) {
            close(listen_fd);
            reset_child_signals();
            serve_client(client_fd);
            close(client_fd);
            _exit(EXIT_SUCCESS);
        }
        close(client_fd);
    }

    if (listen_fd >= 0) close(listen_fd);
    /* Los hijos heredan el grupo de procesos del servidor: terminacion limpia. */
    kill(0, SIGTERM);
    while (waitpid(-1, NULL, 0) > 0) {}
    return EXIT_SUCCESS;
}
