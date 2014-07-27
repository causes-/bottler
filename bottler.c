#define _POSIX_SOURCE
#define _GNU_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define VERBOSE	(1 << 0)
#define CMDS	(1 << 1)
#define RECV	(1 << 2)
#define SEND	(1 << 3)
#define URL		(1 << 4)

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

void *erealloc(void *p, size_t size) {
	void *r = realloc(p, size);
	if (!r)
		eprintf("out of memory\n");
	return r;
}

char *splitstr(char *s, char c) {
	while (*s != c && *s != '\0')
		s++;
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

int putfd(int fd, int opts, char *fmt, ...) {
	int i;
	char *bufout = emalloc(BUFSIZ+1);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(bufout, BUFSIZ+1, fmt, ap);
	va_end(ap);
	strncat(bufout, "\r\n", BUFSIZ+1);

	if (opts & RECV)
		printf("send:%s", bufout);
	i = send(fd, bufout, strlen(bufout), 0);
	free(bufout);
	return i;
}

int getfd(int fd, int opts, char *bufin) {
	int i;
	i = recv(fd, bufin, BUFSIZ, 0);
	if (i != -1)
		bufin[i] = '\0';
	if (opts & RECV && i)
		printf("recv:%s", bufin);
	return i;
}

char *xmlstr(char *s, char *t) {
	char *b = emalloc(strlen(t)+3);
	char *e = emalloc(strlen(t)+4);
	sprintf(b, "<%s>", t);
	sprintf(e, "</%s>", t);
	s = strcasestr(s, b);
	if (s) {
		s = skipstr(s, b);
		t = strcasestr(s, e);
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

char *urltitle(char *url, int opts) {
	int fd;
	char *bufurl;
	char *host, *page, *useragent, *title = NULL;

	host = skipstr(url, "http://");
	page = splitstr(host, '/');

	fd = contohost(host, "80");
	if (fd == -1)
		return NULL;

	useragent = "Mozilla/5.0 (X11; Linux x86_64; rv:30.0) Gecko/20100101 Firefox/30.0";
	putfd(fd, opts, "GET http://%s/%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n",
			host, page, host, useragent);

	bufurl = emalloc(BUFSIZ+1);
	getfd(fd, opts, bufurl);
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
	int opts = 0;
	char *bufin;
	char *usr, *cmd, *chan, *msg;
	char *url, *title;

	for (i = 1; i < argc; i++) {
		if (!strcmp("-v", argv[i]))
			opts |= CMDS | SEND | RECV;
		else if (!strcmp("-c", argv[i]))
			opts |= CMDS;
		else if (!strcmp("-i", argv[i]))
			opts |= RECV;
		else if (!strcmp("-o", argv[i]))
			opts |= SEND;
		else if (!strcmp("-u", argv[i]))
			opts |= URL;
		else if (argv[i+1] == NULL || argv[i+1][0] == '-')
			eprintf("usage: %s [options]\n"
					"-h               help\n"
					"-v               verbose\n"
					"-c               print received commands\n"
					"-i               print received data\n"
					"-o               print sent data\n"
					"-u               disable urltitle\n"
					"-s <host:port>   server hostname\n"
					"-n <nick> [name] bot nickname and name\n", argv[0]);
		else if (!strcmp("-s", argv[i])) {
			host = argv[++i];
			if (strlen(host) != strcspn(host, ":"))
				port = splitstr(host, ':');
		} else if (!strcmp("-n", argv[i])) {
			nick = argv[++i];
			while (argv[i+1] != NULL && argv[i+1][0] != '-') {
				if (!strcmp("-n", argv[i]))
					name = argv[++i];
				else if (!strcmp("-n", argv[i-1])) {
					name = emalloc(strlen(argv[i]) + strlen(argv[i+1]) + 2);
					strcpy(name, argv[i]);
					strcat(name, " ");
					strcat(name, argv[++i]);
				} else {
					erealloc(name, strlen(name) + strlen(argv[i+1]) + 2);
					strcat(name, " ");
					strcat(name, argv[++i]);
				}
			}
		}
	}

	fd = contohost(host, port);
	if (fd == -1)
		eprintf("failed to connect %s:%s\n", host, port);

	putfd(fd, opts, "NICK %s", nick);
	putfd(fd, opts, "USER %s 0 * :%s", nick, name);

	bufin = emalloc(BUFSIZ+1);

	while(1) {
		i = getfd(fd, opts, bufin);
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
			if (opts & CMDS)
				printf("usr:%s, cmd:%s, chan:%s, msg:%s\n", usr, cmd, chan, msg);

			if(!strcmp("PING", cmd))
				putfd(fd, opts, "PONG %s", msg);
			else if (msg[0] == '!') {
				if (msg[1] == 'p' && chan[0] == '#')
					putfd(fd, opts, "PART %s", chan);
				else if (msg[1] == 'j' && msg[3] == '#') {
					putfd(fd, opts, "JOIN %s", msg+3);
					putfd(fd, opts, "PRIVMSG %s :I was invited by %s. Try !h to see my commands.",
							msg+3, usr);
				} else
					putfd(fd, opts, "PRIVMSG %s :Usage: !h help, !p part, !j join",
							chan[0] == '#' ? chan : usr);
			} else if (chan[0] == '#') {
				if (opts & URL) {
					url = isurl(msg);
					if (url) {
						title = urltitle(url, opts);
						if (title) {
							putfd(fd, opts, "PRIVMSG %s :%s", chan, title);
							free(title);
						}
					}
				} else if (!strcmp("1/0", msg)) {
					putfd(fd, opts, "QUIT :division by zero");
					close(fd);
					return EXIT_SUCCESS;
				}
			}
		}
	}
}
