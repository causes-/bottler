#define _POSIX_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

char *host = "irc.quakenet.org";
char *port = "6667";
char *nick = "bottler";
char *name = "bottler bot";

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

char *splitstr(char *s, char c) {
	while (*s != c && *s != '\0')
		s++;
	if (*s != '\0')
		*s = '\0';
	if (*(s-1) == ' ')
		*(s-1) = '\0';
	return ++s;
}

char *skipstr(char *s, char *c) {
	while (*c != '\0')
		if (*s == *c++)
			s++;
	return s;
}

char *splitline(char **s) {
	char *b = *s;
	char *e = *s;
	b = e = *s;
	while (*e != '\n' && *e != '\r' && *e != '\0')
		e++;
	if (b == e)
		return NULL;
	while (*e == '\n' || *e == '\r' || *e == ' ')
		*e++ = '\0';
	*s = e;
	return b;
}

int contohost(const char *host, const char *port) {
	int fd;
	struct addrinfo hints, *res, *r;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	getaddrinfo(host, port, &hints, &res);
	for (r = res; r; r = r->ai_next) {
		fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
		if (fd == -1)
			continue;
		if (connect(fd, r->ai_addr, r->ai_addrlen) == 0)
			break;
		close(fd);
	}
	freeaddrinfo(res);
	if (!r)
		return -1;
	return fd;
}

int putfd(int fd, char *fmt, ...) {
	int i;
	char *bufout = emalloc(BUFSIZ+1);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(bufout, BUFSIZ+1, fmt, ap);
	va_end(ap);
	strncat(bufout, "\r\n", BUFSIZ+1);

	printf("send:%s", bufout);
	i = send(fd, bufout, strlen(bufout), 0);
	free(bufout);
	return i;
}

int getfd(int fd, char *bufin) {
	int i;
	i = recv(fd, bufin, BUFSIZ, 0);
	if (i != -1)
		bufin[i] = '\0';
	if (i > 0)
		printf("recv:%s", bufin);
	return i;
}

char *xmlstr(char *s, char *t) {
	char *b = emalloc(strlen(t)+3);
	char *e = emalloc(strlen(t)+4);
	sprintf(b, "<%s>", t);
	sprintf(e, "</%s>", t);
	s = strstr(s, b);
	if (s) {
		s = skipstr(s, b);
		t = strstr(s, e);
		if (t) {
			*t = '\0';
			s = strdup(s);
		} else
			s = NULL;
	}
	free(b);
	free(e);
	return s;
}

char *urltitle(char *url) {
	int i, fd;
	char *bufurl;
	char *host, *page, *useragent, *title = NULL;

	host = skipstr(url, "http://");
	page = splitstr(host, '/');

	fd = contohost(host, "80");
	if (fd == -1)
		return NULL;

	useragent = "Mozilla/5.0 (X11; Linux x86_64; rv:30.0) Gecko/20100101 Firefox/30.0";
	putfd(fd, "GET http://%s/%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n",
			host, page, host, useragent);

	bufurl = malloc(BUFSIZ+1);
	i = getfd(fd, bufurl);
	if (i > 0)
		title = xmlstr(bufurl, "title");
	free(bufurl);
	close(fd);
	return title;
}

char *isurl(char *url) {
	char *r;
	r = strstr(url, "http://");
	if (!r)
		r = strstr(url, "www.");
	if (r)
		splitstr(r, ' ');
	return r;
}

int main(int argc, char *argv[]) {
	int i, fd;
	char *bufin;
	char *usr, *cmd, *chan, *msg;
	char *url, *title;

	fd = contohost(host, port);
	if (fd == -1)
		eprintf("failed to connect %s:%s\n", host, port);

	putfd(fd, "NICK %s", nick);
	putfd(fd, "USER %s 0 * :%s", nick, name);

	bufin = emalloc(BUFSIZ+1);

	while(1) {
		i = getfd(fd, bufin);
		if (i < 1)
			continue;
		while ((cmd = splitline(&bufin)) != NULL) {
			usr = host;
			if (cmd[0] == ':') {
				usr = cmd+1;
				cmd = splitstr(usr, ' ');
				splitstr(usr, '!');
			}
			chan = splitstr(cmd, ' ');
			msg = splitstr(chan, ':');

			if(!strcmp("PING", cmd))
				putfd(fd, "PONG %s", msg);
			else if (msg[0] == '!') {
				if (msg[1] == 'p' && chan[0] == '#')
					putfd(fd, "PART %s", chan);
				else if (msg[1] == 'j' && msg[3] == '#') {
					putfd(fd, "JOIN %s", msg+3);
					putfd(fd, "PRIVMSG %s :I was invited by %s. Try !h to see my commands.",
							msg+3, usr);
				} else
					putfd(fd, "PRIVMSG %s :Usage: !h help, !p part, !j join",
							chan[0] == '#' ? chan : usr);
			} else if (chan[0] == '#') {
				url = isurl(msg);
				if (url) {
					title = urltitle(url);
					if (title) {
						putfd(fd, "PRIVMSG %s :%s", chan, title);
						free(title);
					}
				}
				if (!strcmp("1/0", msg)) {
					putfd(fd, "QUIT :division by zero");
					close(fd);
					return EXIT_SUCCESS;
				}
			}
		}
	}
}
