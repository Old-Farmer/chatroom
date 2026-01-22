#include "setup.h"

#include <assert.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

void log_err(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

void *xmalloc(size_t s) {
    void *p = malloc(s);
    if (p == NULL) {
        log_err("OMM\n");
        exit(1);
    }
    return p;
}

void xfree(void *p) { free(p); }

void *xrealloc(void *p, size_t new_s) {
    assert(p != NULL);
    void *new_p = realloc(p, new_s);
    if (new_p == NULL) {
        log_err("OMM\n");
        exit(1);
    }
    return new_p;
}

void *xcalloc(size_t nmemb, size_t s) {
    void *p = calloc(nmemb, s);
    if (p == NULL) {
        log_err("OMM\n");
        exit(1);
    }
    return p;
}

void nodelay(int fd) {
    int enable = 1;
    int rc = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
    assert(rc == 0);
}

void keepalive(int fd) {
    int enable = 1;
    int rc;
    rc = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
    assert(rc == 0);

    int idle = 10;
    int interval = 3;
    int count = 3;

    rc = setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    assert(rc == 0);
    rc =
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    assert(rc == 0);
    rc = setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
    assert(rc == 0);
}

void reuseaddr(int fd) {
    int enable = 1;
    int rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    assert(rc == 0);
}
