#ifndef UTIL_H
#define UTIL_H

void eprintf(const char *fmt, ...);

void *emalloc(size_t size);

void *erealloc(void *p, size_t size);

void *estrdup(void *p);

#endif
