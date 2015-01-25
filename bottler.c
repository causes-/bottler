#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>

#include "config.h"
#include "util.h"
#include "gettitle.h"

#define VERSION "0.1"

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

int dial(const char *host, const char *port) {
	int fd;
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *r;

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

void corejobs(FILE *srv, struct command c) {
	char *channel;

	if (!strncmp(nick, c.msg, strlen(nick))) {
		sendf(srv, "PRIVMSG %s :Try !h for help",
				c.par[0] == '#'  ? c.par : c.nick);
	} else if (!strncmp("!h", c.msg, 2)) {
		sendf(srv, "PRIVMSG %s :Usage: !h help, !v version, !o owner, !j join, !p part",
				c.par[0] == '#'  ? c.par : c.nick);
	} else if (!strncmp("!v", c.msg, 2)) {
		sendf(srv, "PRIVMSG %s :Bottler IRC-bot %s",
				c.par[0] == '#'  ? c.par : c.nick, VERSION);
	} else if (!strncmp("!o", c.msg, 2)) {
		sendf(srv, "PRIVMSG %s :My owner: %s",
				c.par[0] == '#'  ? c.par : c.nick, owner);
	} else if (!strcmp(c.mask, owner)) {
		if (strlen(c.msg) > 3 && !strncmp("!j", c.msg, 2)) {
			if (c.msg[3] == '#')
				sendf(srv, "JOIN %s", c.msg + 3);
			else {
				channel = emalloc(strlen(c.msg + 3) + 1);
				sprintf(channel, "#%s", c.msg + 3);
				sendf(srv, "JOIN %s", channel);
				free(channel);
			}
		} else if (strlen(c.msg) > 1 && !strncmp("!p", c.msg, 2)) {
			if (c.par[0] == '#')
				sendf(srv, "PART %s", c.par);
		} else if (strlen(c.msg) > 3 && !strncmp("!p ", c.msg, 3)) {
			if (c.msg[3] == '#')
				sendf(srv, "PART %s", c.msg + 3);
			else {
				channel = emalloc(strlen(c.msg + 3) + 1);
				sprintf(channel, "#%s", c.msg + 3);
				sendf(srv, "PART %s", channel);
				free(channel);
			}
		}
	}
}

void urljobs(FILE *srv, struct command c) {
	char *url;
	char *title;

	if (c.par[0] == '#') {
		url = strcasestr(c.msg, "http://");
		if (!url)
			url = strcasestr(c.msg, "https://");
		if (!url)
			url = strcasestr(c.msg, "www.");
		if (url) {
			url = estrdup(url);
			skip(url, ' ');
			trim(url);
			title = gettitle(url);
			if (title) {
				sendf(srv, "PRIVMSG %s :%s", c.par, title);
				free(title);
			}
			free(url);
		}
	}
}

void parseline(FILE *srv, char *line) {
	struct command c;
	time_t t;
	struct tm *tm;

	t = time(NULL);
	tm = localtime(&t);
	skip(line, '\r');
	printf("[%.2d:%.2d:%.2d] >%s\n", tm->tm_hour, tm->tm_min, tm->tm_sec, line);

	c.cmd = line;
	c.nick = host;
	c.mask = "";
	if (*c.cmd == ':') {
		c.nick = c.cmd+1;
		c.cmd = skip(c.nick, ' ');
		c.mask = skip(c.nick, '!');
	}
	c.par = skip(c.cmd, ' ');
	c.msg = skip(c.par, ':');
	trim(c.par);

	if (!strcmp("PING", c.cmd))
		sendf(srv, "PONG %s", c.msg);

	corejobs(srv, c);

	urljobs(srv, c);
}

int main(int argc, char **argv) {
	int i;
	char buf[BUFSIZ];
	int fd;
	FILE *srv;
	fd_set readfds;

	for (i = 1; i < argc; i++) {
		if (!strcmp("-v", argv[i]))
			eprintf("%s-%s\n", argv[0], VERSION);
		else if (argv[i+1] == NULL || argv[i+1][0] == '-')
			eprintf("usage: %s [options]\n"
					"\n"
					"-h    help\n"
					"-v    version\n"
					"\n"
					"-s <host>     server hostname\n"
					"-p <port>     server port\n"
					"-n <nick>     bot nickname\n"
					"-u <name>     bot username\n"
					"-o <owner>    bot owner\n"
					, argv[0]);
		else if (!strcmp("-s", argv[i]))
			host = argv[++i];
		else if (!strcmp("-p", argv[i]))
			port = argv[++i];
		else if (!strcmp("-n", argv[i]))
			nick = argv[++i];
		else if (!strcmp("-u", argv[i]))
			name = argv[++i];
		else if (!strcmp("-o", argv[i]))
			owner = argv[++i];
	}

	if (!host || !port)
		eprintf("you need to specify host and port\n");
	if (!nick || !name)
		eprintf("you need to specify nick and name\n");
	if (!owner)
		eprintf("you need to specify owner\n");

	fd = dial(host, port);
	if (fd == -1)
		eprintf("failed to connect to %s:%s\n", host, port);
	else
		printf("connected to %s:%s\n", host, port);

	srv = fdopen(fd, "r+");

	sendf(srv, "NICK %s", nick);
	sendf(srv, "USER %s 0 * :%s", nick, name);

	fflush(srv);
	setbuf(srv, NULL);

	while (1) {
		FD_ZERO(&readfds);
		FD_SET(fileno(srv), &readfds);

		if (select(fileno(srv) + 1, &readfds, 0, 0, &(struct timeval){120, 0})) {
			if (FD_ISSET(fileno(srv), &readfds)) {
				if (!fgets(buf, sizeof buf, srv))
					eprintf("host closed connection\n");
				parseline(srv, buf);
			}
		} else {
			sendf(srv, "PING %s", host);
		}
	}
}
