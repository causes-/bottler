#ifndef UTIL_H
#define UTIL_H

struct command {
	char *nick;
	char *mask;
	char *cmd;
	char *par;
	char *msg;
};

void eprintf(const char *fmt, ...);

void *emalloc(size_t size);

void *erealloc(void *p, size_t size);

void *estrdup(void *p);

#endif
