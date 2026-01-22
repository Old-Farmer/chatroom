#pragma once

#include <stddef.h>
#include <stdbool.h>

// Print to stderr, no \n
void log_err(const char *format, ...);

// Malloc/Free but never fail
void *xmalloc(size_t s);
void xfree(void *p);
void *xrealloc(void *p, size_t new_s);
void *xcalloc(size_t nmemb, size_t s);

// Network stuf
void nodelay(int fd);
void keepalive(int fd);
void reuseaddr(int fd);
