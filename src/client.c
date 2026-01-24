
#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "setup.h"

struct termios orig_termios;
bool need_restore = false;
void term_restore() {
    if (need_restore) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        need_restore = false;
    }
}
// Disable line buffering
void term_set() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(term_restore);

    struct termios t = orig_termios;
    t.c_lflag &= ~ICANON;
    t.c_lflag &= ~ECHO;

    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
    need_restore = true;
}

#define PROMPT "> "

#define CLEARLINE_AND_HOME "\033[2K\r"
#define DELETE_ONE "\033[D\033[K"

typedef struct {
    int sock;
    const char *name;  // Name of the user
    bool quit;
} Client;

typedef struct {
    size_t cap;
    char *data;
    size_t len;
} DynamicBuf;
#define DYNAMIC_BUF_INIT_SIZE 128

static void buf_init(DynamicBuf *self);
static void buf_append(DynamicBuf *self, const char *buf, size_t s);
static void buf_popback(DynamicBuf *self, size_t s);
static void buf_clear(DynamicBuf *self);
// static void buf_free(DynamicBuf *self);

static void client_init(Client *self, const char *name);
static bool client_connect(Client *self, const char *addr, const char *port);

static void repl(Client *client);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        log_err("Usage: %s <ip> <port> <name>\n", argv[0]);
        exit(1);
    }
    Client client;
    client_init(&client, argv[3]);
    if (!client_connect(&client, argv[1], argv[2])) {
        exit(1);
    }
    printf("Connected to server!\n");
    repl(&client);
    term_restore();
    printf("\nByeBye!\n");
}

static void client_init(Client *self, const char *name) {
    *self = (Client){.name = name};
}

static bool client_connect(Client *self, const char *addr, const char *port) {
    int rc;
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0,
        .ai_flags = 0,
    };
    struct addrinfo *res;
    rc = getaddrinfo(addr, port, &hints, &res);
    if (rc != 0) {
        log_err("getaddrinfo error: %s\n", gai_strerror(rc));
        return false;
    }
    struct addrinfo *rp;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        self->sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (self->sock == -1) {
            log_err("Create socket error: %s\n", strerror(errno));
            continue;
        }
        nodelay(self->sock);
        do {
            rc = connect(self->sock, rp->ai_addr, rp->ai_addrlen);
        } while (rc == -1 && errno != EINTR);
        if (rc == 0) {
            break;
        }
        log_err("Connect error: %s\n", strerror(errno));
        close(self->sock);
        self->sock = -1;
    }
    freeaddrinfo(res);
    return rp != NULL;
}

#define BUF_SIZE 1024

static void repl(Client *client) {
    char buf[BUF_SIZE];
    term_set();
    ignore_sigpipe();

    int epoll_fd = epoll_create(1);
    if (epoll_fd == -1) {
        log_err("epoll_create error: %s\n", strerror(errno));
        exit(1);
    }

    int infd = fileno(stdin);
    struct epoll_event ev[2];
    ev[0].events = EPOLLIN;
    ev[0].data.fd = infd;
    ev[1].events = EPOLLIN;
    ev[1].data.fd = client->sock;
    for (int i = 0; i < 2; i++) {
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ev[i].data.fd, &ev[i]) == -1) {
            log_err("epoll_ctl error: %s\n", strerror(errno));
            return;
        }
    }

    DynamicBuf user_buf;
    buf_init(&user_buf);
    DynamicBuf recv_buf;
    buf_init(&recv_buf);
    DynamicBuf send_buf;
    buf_init(&send_buf);

    // not yet name == 0
    // name parsed == 1
    // msg parsed == 2
    int recv_state = 0;

    fputs(PROMPT, stderr);
    while (!client->quit) {
        int rc = epoll_wait(epoll_fd, ev, 2, -1);
        if (rc == -1 && rc != EINTR) {
            log_err("epoll_ctl error: %s\n", strerror(errno));
            break;
        } else if (rc <= 0) {
            continue;
        }

        for (int i = 0; i < rc; i++) {
            int fd = ev[i].data.fd;
            if (fd == infd) {  // user input
                int s = read_inter_retry(fd, buf, BUF_SIZE);
                assert(s != -1 && s != 0);
                for (int j = 0; j < s; j++) {
                    // We only care about ascii sequence
                    // TODO: utf-8 support
                    if (buf[j] >= 32 && buf[j] != 127) {
                        buf_append(&user_buf, &buf[j], 1);
                        printf("%c", buf[j]);
                        continue;
                    }
                    if (buf[j] == '\n') {
                        fputs("\n" PROMPT, stderr);

                        if (user_buf.len == 0) {
                            continue;
                        }

                        // pack msg and send it to network
                        buf_append(&send_buf, client->name,
                                   strlen(client->name) + 1);
                        buf_append(&send_buf, (char *)user_buf.data,
                                   user_buf.len);
                        buf_append(&send_buf, "\0", 1);
                        if (!writeall(client->sock, send_buf.data,
                                      send_buf.len)) {
                            client->quit = true;
                            fputs(CLEARLINE_AND_HOME, stdout);
                            printf("write error\n");
                            break;
                        }
                        buf_clear(&send_buf);
                        buf_clear(&user_buf);
                    } else if (buf[j] == 4) {  // Ctrl-D
                        client->quit = true;
                        break;
                    } else if (buf[j] == 8 || buf[j] == 127) {  // backspace
                        if (user_buf.len == 0) {
                            continue;
                        }
                        fputs(DELETE_ONE, stderr);
                        buf_popback(&user_buf, 1);
                    } else {
                        // discard
                    }
                }
                if (user_buf.len != 0) {
                    fflush(stdout);
                }
            } else {  // network recv
                if (ev[i].events & EPOLLERR) {
                    fputs(CLEARLINE_AND_HOME, stdout);
                    printf("socket error\n");
                    client->quit = true;
                    continue;
                } else if (ev[i].events & EPOLLHUP) {
                    fputs(CLEARLINE_AND_HOME, stdout);
                    fputs("The server close the connect\n", stdout);
                    client->quit = true;
                    continue;
                }
                ssize_t s = read(fd, buf, BUF_SIZE);
                if (s == 0) {
                    fputs(CLEARLINE_AND_HOME, stdout);
                    fputs("The server close the connect\n", stdout);
                    client->quit = true;
                    continue;
                } else if (s == -1) {
                    fputs(CLEARLINE_AND_HOME, stdout);
                    printf("read error: %s\n", strerror(errno));
                    client->quit = true;
                    continue;
                }

                for (int j = 0; j < s; j++) {
                    if (buf[j] == '\0') {
                        recv_state++;
                    }
                    buf_append(&recv_buf, &buf[j], 1);
                    if (recv_state == 2) {
                        fputs(CLEARLINE_AND_HOME, stdout);

                        // print network msg
                        printf("%s: %s\n", recv_buf.data,
                               recv_buf.data + strlen(recv_buf.data) + 1);

                        // restore user input
                        printf(PROMPT "%.*s", (int)user_buf.len, user_buf.data);
                        fflush(stdout);

                        buf_clear(&recv_buf);
                        recv_state = 0;
                    }
                }
            }
        }
    }
    close(epoll_fd);
    close(client->sock);
}

static void buf_init(DynamicBuf *self) {
    *self = (DynamicBuf){.data = xmalloc(DYNAMIC_BUF_INIT_SIZE),
                         .len = 0,
                         .cap = DYNAMIC_BUF_INIT_SIZE};
}

static void buf_append(DynamicBuf *self, const char *buf, size_t s) {
    if (self->len + s > self->cap) {
        self->data = xrealloc(self->data, self->len + s);
        self->cap = self->len + s;
    }
    memcpy(self->data + self->len, buf, s);
    self->len += s;
}

static void buf_popback(DynamicBuf *self, size_t s) {
    if (self->len < s) {
        self->len = 0;
    } else {
        self->len -= s;
    }
}

static void buf_clear(DynamicBuf *self) { self->len = 0; }

// static void buf_free(DynamicBuf *self) {
//     self->len = self->cap = 0;
//     xfree(self->data);
// }
