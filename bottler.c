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

struct command {
	char *nick;
	char *host;
	char *cmd;
	char *par;
	char *msg;
};

static sig_atomic_t quit;
static sig_atomic_t dflag;

char *host = "irc.server.org";
char *port = "6667";
char *nick = "bottler";
char *name = "bottler bot";
char *admin = "~nick@192-0-2-131.hostname.net";
char *config;

void eprintf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void sighandler(int sig) {
	if (sig == SIGINT) {
		if (quit == 1)
			eprintf("terminating immediately");
		else {
			puts("closing connections");
			quit = 1;
		}
		signal(SIGINT, sighandler);
	}
}

void *emalloc(size_t size) {
	void *p = malloc(size);
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

int readconfig(void) {
	FILE *fp;
	char *line = NULL;
	char *p;
	size_t n = 0;

	fp = fopen(config, "r");
	if (fp == NULL) {
		fprintf(stderr, "can't open %s\n", config);
		return 0;
	}
	while (getline(&line, &n, fp) != -1) {
		line = splitline(&line);
		if (!line)
			continue;
		p = strstr(line, "daemon");
		if (p)
			dflag = 1;
		p = strstr(line, "admin=");
		if (p) {
			p += 9;
			admin = estrdup(p);
		}
		p = strstr(line, "server=");
		if (p) {
			p += 7;
			host = estrdup(p);
		}
		p = strstr(line, "port=");
		if (p) {
			p += 5;
			port = estrdup(p);
		}
		p = strstr(line, "nick=");
		if (p) {
			p += 5;
			nick = estrdup(p);
		}
		p = strstr(line, "name=");
		if (p) {
			p += 5;
			name = estrdup(p);
		}
	}
	free(line);
	fclose(fp);
	return 1;
}

int contohost(const char *host, const char *port) {
	int fd;
	struct addrinfo hints, *res = NULL, *r = NULL;

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

void parseline(char *line, struct command *c) {
	c->cmd = line;
	c->nick = host;
	c->host = "";
	if (!strncmp(c->cmd, ":", 1)) {
		c->nick = c->cmd+1;
		c->cmd = splitstr(c->nick, ' ');
		c->host = splitstr(c->nick, '!');
	}
	c->par = splitstr(c->cmd, ' ');
	c->msg = splitstr(c->par, ':');
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

char *httpgetxml(char *url, char *xmltag) {
	int urlfd;
	char *bufurl;
	char *host, *page, *value = NULL;

	host = url;
	page = splitstr(host, '/');
	urlfd = contohost(host, "80");
	if (urlfd == -1)
		return 0;
	putfd(urlfd, "GET http://%s/%s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:30.0) Gecko/20100101 Firefox/30.0\r\n"
			, host, page, host);
	bufurl = emalloc(BUFSIZ+1);
	while (getfd(urlfd, bufurl) && !value)
		value = xmlstr(bufurl, xmltag);
	free(bufurl);
	close(urlfd);
	return value;
}

int coreopts(struct command c, int fd) {
	if (!strncmp("!p", c.msg, 2)) {
		if (!strncmp("#", c.par, 1)) {
			putfd(fd, "PART %s", c.par);
			return 1;
		}
	} else if (!strncmp("!o", c.msg, 2)) {
		putfd(fd, "PRIVMSG %s :My owner: %s",
				!strncmp("#", c.par, 1) ? c.par : c.nick,
				c.host);
		return 1;
	} else if (!strncmp("!m", c.msg, 2)) {
		putfd(fd, "PRIVMSG %s :%s",
				!strncmp("#", c.par, 1) ? c.par : c.nick,
				c.host);
		return 1;
	} else if (!strncmp("!h", c.msg, 2)) {
		putfd(fd, "PRIVMSG %s :Usage: !h help, !p part, !j join, !m hostmask, !o owner",
				!strncmp("#", c.par, 1) ? c.par : c.nick);
		return 1;
	} else if (!strncmp("!j #", c.msg, 4)) {
		putfd(fd, "JOIN %s", c.msg+3);
		putfd(fd, "PRIVMSG %s :I was invited by %s. Try !h to see my commands.", c.msg+3, c.nick);
		return 1;
	}
	return 0;
}

int adminopts(struct command c, int fd) {
	if (!strcmp(c.host, admin) && !!strncmp("#", c.par, 1)) {
		if (!strncmp("!q", c.msg, 2)) {
			putfd(fd, "PRIVMSG %s :closing connections", c.nick);
			quit = 1;
			return 1;
		} else if (!strncmp("!h", c.msg, 2)) {
			putfd(fd, "PRIVMSG %s :Admin: !q quit", c.nick);
			return 1;
		}
	}
	return 0;
}

int urlopts(struct command *c, int fd) {
	char *url, *title;
	if (!strncmp("#", c->par, 1)) {
		url = strstr(c->msg, "http://");
		if (url)
			url += 7;
		else {
			url = strstr(c->msg, "https://");
			if (url)
				url += 8;
			else
				url = strstr(c->msg, "www.");
		}
		if (url) {
			splitstr(url, ' ');
			title = httpgetxml(url, "title");
			if (title) {
				putfd(fd, "PRIVMSG %s :%s", c->par, title);
				free(title);
				return 1;
			}
		}
	}
	return 0;
}

int main(int argc, char *argv[]) {
	int i, fd;
	char *bufin, *line;
	struct command c;
	char *username;

	username = getenv("USER");
	if (username) {
		config = emalloc(strlen(username)+22);
		sprintf(config, "/home/%s/.bottler.conf", username);
		readconfig();
	}

	for (i = 1; i < argc; i++) {
		if (!strcmp("-d", argv[i])) {
			dflag = 1;
		} else if (argv[i+1] == NULL || argv[i+1][0] == '-') {
			eprintf("usage: %s [options]\n"
					"\n"
					"-h               help\n"
					"-d               run as daemon\n"
					"-f <file>        config file\n"
					"-s <host:port>   server hostname\n"
					"-n <nick> [name] bot nickname\n"
					"-n <name>        bot name\n"
					"-m <hostmask>    admin hostmask\n"
					, argv[0]);
		} else if (!strcmp("-f", argv[i])) {
			free(config);
			config = argv[++i];
			readconfig();
		} else if (!strcmp("-s", argv[i])) {
			free(host);
			host = argv[++i];
			if (strlen(host) != strcspn(host, ":")) {
				free(port);
				port = splitstr(host, ':');
			}
		} else if (!strcmp("-m", argv[i])) {
			free(admin);
			admin = argv[++i];
		} else if (!strcmp("-n", argv[i])) {
			free(nick);
			nick = argv[++i];
		} else if (!strcmp("-N", argv[i])) {
			free(name);
			name = argv[++i];
			if (argv[i+1] != NULL && argv[i+1][0] != '-') {
				name = emalloc(strlen(argv[i]) + strlen(argv[i+1]) + 2);
				strcpy(name, argv[i]);
				strcat(name, " ");
				strcat(name, argv[++i]);
			}
		}
	}

	printf("nick:%s\nname:%s\nserver:%s:%s\nadmin:%s\nconfig:%s\n",
			nick, name, host, port, admin, config);

	if (dflag)
		if (daemon(1,0) != 0)
			eprintf("failed to daemonize");

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
		while ((line = splitline(&bufin)) != NULL) {
			parseline(line, &c);

			if(!strcmp("PING", c.cmd))
				putfd(fd, "PONG %s", c.msg);

			if (coreopts(c, fd))
				continue;
			if (adminopts(c, fd))
				continue;
			if (urlopts(&c, fd))
				continue;
		}
	}

	putfd(fd, "QUIT");
	close(fd);
	return EXIT_SUCCESS;
}
