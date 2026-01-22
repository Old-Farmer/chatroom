// A chat server

#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "setup.h"

#define MAX_CONN 1024
#define LISTEN_BACKLOG 512

typedef struct {
    int conns[MAX_CONN];  // -1 means no conn
    int conns_max_size;
    int sock;
} Server;

static bool server_init(Server *self, const char *addr, const char *port);
static void server_run(Server *self);
// return the socket fd, -1 means error.
static int create_listening_socket(const char *addr, const char *port);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        log_err("Usage: %s <ip> <port>\n", argv[0]);
        exit(1);
    }
    Server server;
    if (!server_init(&server, argv[1], argv[2])) {
        exit(1);
    }
    server_run(&server);
}

static bool server_init(Server *self, const char *addr, const char *port) {
    for (int i = 0; i < MAX_CONN; i++) {
        self->conns[i] = -1;
    }
    self->conns_max_size = 0;
    self->sock = create_listening_socket(addr, port);
    return self->sock != -1;
}

#define BUF_SIZE 1024
#define EPOLL_EVENT_SIZE 1024

static void server_run(Server *self) {
    int epoll_fd = epoll_create(1);
    if (epoll_fd == -1) {
        log_err("epoll_create error: %s\n", strerror(errno));
        exit(1);
    }

    struct epoll_event ev[EPOLL_EVENT_SIZE];
    ev[0].events = EPOLLIN;
    ev[0].data.fd = self->sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, self->sock, ev) == -1) {
        log_err("epoll_ctl error: %s\n", strerror(errno));
        return;
    }

    char buf[BUF_SIZE];
    while (true) {
        int rc = epoll_wait(epoll_fd, ev, EPOLL_EVENT_SIZE, -1);
        if (rc == -1 && rc != EINTR) {
            log_err("epoll_wait error: %s\n", strerror(errno));
            break;
        }
        for (int i = 0; i < rc; i++) {
            int fd = ev[i].data.fd;
            if (fd == self->sock) {
                // new connection
                int client_fd = accept(self->sock, NULL, 0);
                if (client_fd == -1) {
                    continue;
                }
                int j;
                for (j = 0; j < self->conns_max_size; j++) {
                    if (self->conns[j] != -1) {
                        continue;
                    }
                    break;
                }
                if (j == MAX_CONN) {
                    // No slot for new connection, force close
                    close(client_fd);
                    continue;
                }
                struct epoll_event new_ev = {.events = EPOLLIN, .data.fd = client_fd};
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &new_ev) == -1) {
                    continue;
                }
                self->conns[j] = client_fd;
                if (j == self->conns_max_size) {
                    self->conns_max_size++;
                }
                printf("New connection: %d\n", client_fd);
            } else {
                int s = read(ev[i].data.fd, buf, BUF_SIZE);
                if (s <= 0) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ev[i].data.fd, NULL);
                    close(fd);
                    for (int j = 0; j < self->conns_max_size; j++) {
                        if (self->conns[j] == fd) {
                            self->conns[i] = -1;
                            if (j == self->conns_max_size - 1) {
                                self->conns_max_size--;
                            }
                            break;
                        }
                    }
                    printf("connection %d disconnected\n", ev[i].data.fd);
                } else {
                    // broadcast
                    for (int j = 0; j < self->conns_max_size; j++) {
                        if (self->conns[j] == -1 || self->conns[j] == fd) {
                            continue;
                        }
                        write(self->conns[j], buf, s);
                    }
                    printf("transfer connection %d msg\n", fd);
                }
            }
        }
    }
}

static int create_listening_socket(const char *addr, const char *port) {
    int rc;
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_PASSIVE,
        .ai_protocol = 0,
    };
    struct addrinfo *res;
    rc = getaddrinfo(addr, port, &hints, &res);
    if (rc != 0) {
        log_err("getaddrinfo error: %s\n", gai_strerror(rc));
        return -1;
    }
    int sock;
    struct addrinfo *rp;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) {
            log_err("Create socket error: %s\n", strerror(errno));
            continue;
        }
        reuseaddr(sock);
        if (bind(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        log_err("Bind error: %s\n", strerror(errno));
    }
    freeaddrinfo(res);
    if (rp == NULL) {
        return -1;
    }
    if (listen(sock, LISTEN_BACKLOG) == -1) {
        log_err("Listen error: %s\n", strerror(errno));
        return -1;
    }
    return sock;
}
