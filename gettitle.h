#ifndef GETTITLE_H
#define GETTITLE_H

struct htmldata {
	char *memory;
	size_t size;
};

char *gettitle(char *url);

#endif
