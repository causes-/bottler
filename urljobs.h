#ifndef URLJOBS_H
#define URLJOBS_H

struct htmldata {
	char *memory;
	size_t size;
};

void urljobs(FILE *srv, struct command c);

#endif
