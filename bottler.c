#define _POSIX_SOURCE
#define _GNU_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

static sig_atomic_t quit;

char *host = "irc.quakenet.org";
char *port = "6667";
char *nick = "bottler";
char *name = "bottler bot";

void sighandler(int sig) {
	if (sig == SIGINT) {
		if (quit == 1) {
			puts("terminating immediately");
			exit(0);
		} else {
			quit = 1;
			puts("closing connections");
		}
		signal(SIGINT, sighandler);
	}
}

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

void *estrdup(void *p) {
	void *r = strdup(p);
	if (!r)
		eprintf("out of memory\n");
	return r;
}

char *splitstr(char *s, char c) {
	while (*s != c && *s != '\0')
		s++;
	if (*s == '\0')
		return "";
	*s = '\0';
	if (*(s-1) == ' ')
		*(s-1) = '\0';
	return s+1;
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
	if (i)
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
		s += strlen(b);
		t = strcasestr(s, e);
		if (t) {
			*t = '\0';
			s = estrdup(s);
		} else
			s = NULL;
	}
	free(b);
	free(e);
	return s;
}

char *urltitle(char *url) {
	int fd;
	char *bufurl;
	char *host, *page, *title = NULL;

	host = url;
	if (!strncmp("http://", host, 7))
		host += 7;
	page = splitstr(host, '/');
	fd = contohost(host, "80");
	if (fd == -1)
		return NULL;

	putfd(fd, "GET http://%s/%s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:30.0) Gecko/20100101 Firefox/30.0\r\n"
			, host, page, host);
	bufurl = emalloc(BUFSIZ+1);
	while (getfd(fd, bufurl) && !title)
		title = xmlstr(bufurl, "title");
	free(bufurl);
	close(fd);
	return title;
}

int main(int argc, char *argv[]) {
	int i, fd;
	char *bufin;
	char *usr, *cmd, *par, *msg;
	char *url, *title;

	for (i = 1; i < argc; i++) {
		if (!strcmp("-d", argv[i])) {
			if (daemon(1,0) != 0)
				eprintf("failed to daemonize");
		} else if (argv[i+1] == NULL || argv[i+1][0] == '-') {
			eprintf("usage: %s [options]\n"
					"\n"
					"-h               help\n"
					"-d               run as daemon\n"
					"-s <host:port>   server hostname\n"
					"-n <nick> [name] bot nickname and name\n"
					, argv[0]);
		} else if (!strcmp("-s", argv[i])) {
			host = argv[++i];
			if (strlen(host) != strcspn(host, ":"))
				port = splitstr(host, ':');
		} else if (!strcmp("-n", argv[i])) {
			nick = argv[++i];
			if (argv[i+1] != NULL && argv[i+1][0] != '-')
				name = argv[++i];
			if (argv[i+1] != NULL && argv[i+1][0] != '-') {
				name = emalloc(strlen(argv[i]) + strlen(argv[i+1]) + 2);
				strcpy(name, argv[i]);
				strcat(name, " ");
				strcat(name, argv[++i]);
			}
			while (argv[i+1] != NULL && argv[i+1][0] != '-') {
				erealloc(name, strlen(name) + strlen(argv[i+1]) + 2);
				strcat(name, " ");
				strcat(name, argv[++i]);
			}
		}
	}

	fd = contohost(host, port);
	if (fd == -1)
		eprintf("failed to connect %s:%s\n", host, port);
	putfd(fd, "NICK %s", nick);
	putfd(fd, "USER %s 0 * :%s", nick, name);

	bufin = emalloc(BUFSIZ+1);
	signal(SIGINT, sighandler);

	while(!quit) {
		i = getfd(fd, bufin);
		if (!i)
			continue;
		while ((cmd = splitline(&bufin)) != NULL) {
			usr = host;
			if (cmd[0] == ':') {
				usr = cmd+1;
				cmd = splitstr(usr, ' ');
				splitstr(usr, '!');
			}
			par = splitstr(cmd, ' ');
			msg = splitstr(par, ':');

			if(!strcmp("PING", cmd))
				putfd(fd, "PONG %s", msg);
			else if (!strncmp("!p ", msg, 3))
				putfd(fd, "PART %s", par);
			else if (!strncmp("!j #", msg, 4)) {
				putfd(fd, "JOIN %s", msg+3);
				putfd(fd, "PRIVMSG %s :I was invited by %s. Try !h to see my commands.",
						msg+3, usr);
			} else if (!strncmp("!", msg, 1)) {
				putfd(fd, "PRIVMSG %s :Usage: !h help, !p part, !j join, !a auth, !q quit",
						par[0] == '#' ? par : usr);
			} else if (!strncmp("#", par, 1)) {
				url = strstr(msg, "http://");
				if (!url)
					url = strstr(msg, "www.");
				if (url) {
					splitstr(url, ' ');
					title = urltitle(url);
					if (title) {
						putfd(fd, "PRIVMSG %s :%s", par, title);
						free(title);
					}
				}
			}
		}
	}
	putfd(fd, "QUIT");
	close(fd);
	return EXIT_SUCCESS;
}
