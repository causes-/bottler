#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

void eprintf(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void *emalloc(size_t size) {
	void *p;

	p = malloc(size);
	if (!p)
		eprintf("out of memory\n");
	return p;
}

void *erealloc(void *p, size_t size) {
	void *r;

	r = realloc(p, size);
	if (!r)
		eprintf("out of memory\n");
	return r;
}

void *estrdup(void *p) {
	void *r;

	r = strdup(p);
	if (!r)
		eprintf("out of memory\n");
	return r;
}

char *skip(char *s, char c) {
	while(*s != c && *s != '\0')
		s++;
	if(*s != '\0')
		*s = '\0';
	return ++s;
}

void trim(char *s) {
	char *e;

	e = s + strlen(s) - 1;
	while(isspace(*e) && e > s)
		e--;
	*(e + 1) = '\0';
}


int sendf(FILE *srv, char *fmt, ...) {
	char buf[BUFSIZ];
	va_list ap;
	time_t t;
	struct tm *tm;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	t = time(NULL);
	tm = localtime(&t);
	printf("[%.2d:%.2d:%.2d] <%s\n", tm->tm_hour, tm->tm_min, tm->tm_sec, buf);

	return fprintf(srv, "%s\r\n", buf);
}
