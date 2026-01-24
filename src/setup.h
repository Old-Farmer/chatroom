#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

// Print to stderr, no \n
void log_err(const char *format, ...);

// Malloc/Free but never fail
void *xmalloc(size_t s);
void xfree(void *p);
void *xrealloc(void *p, size_t new_s);
void *xcalloc(size_t nmemb, size_t s);

// Network stuff
void nodelay(int fd);
void keepalive(int fd);
void reuseaddr(int fd);
ssize_t read_inter_retry(int fd, void* buf, size_t s);
ssize_t write_inter_retry(int fd, void* buf, size_t s);
bool writeall(int fd, void* buf, size_t s); // blocking writeall

// signal stuff
void ignore_sigpipe();
