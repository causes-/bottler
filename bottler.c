#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>

#include "arg.h"
#include "config.h"
#include "util.h"
#include "gettitle.h"

#define VERSION "0.2"

struct command {
	char *nick;
	char *mask;
	char *cmd;
	char *par;
	char *msg;
};

char *argv0;

int afclose(FILE **stream) {
	int r;

	r = fclose(*stream);
	if (r == 0)
		*stream = NULL;
	return r;
}

char *skip(char *s, char c) {
	if (!s)
		return NULL;

	while (*s != c && *s != '\0')
		s++;

	if (*s == '\0')
		return NULL;
	else
		*s = '\0';

	return ++s;
}

void trim(char *s) {
	char *e;

	if (!s)
		return;

	e = s + strlen(s) - 1;

	while (isspace(*e) && e > s)
		e--;

	*(e + 1) = '\0';
}

int dial(const char *host, const char *port) {
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

void joinpart(FILE *srv, char *chan, bool join) {
	char *channel;

	if (chan[0] == '#') {
		sendf(srv, "%s %s", join ? "JOIN" : "PART", chan);
	} else {
		channel = emalloc(strlen(chan) + 1);
		sprintf(channel, "#%s", chan);
		sendf(srv, "%s %s", join ? "JOIN" : "PART", channel);
		free(channel);
	}
}

void autojoin(FILE *srv) {
	char *chanlist, *p, *p2;

	chanlist = estrdup(channels);

	for (p = chanlist; p; p = p2) {
		p2 = skip(p, ' ');
		joinpart(srv, p, true);
	}

	free(chanlist);
}

void corejobs(FILE *srv, struct command c) {
	if (!strncmp(nick, c.msg, strlen(nick)))
		sendf(srv, "PRIVMSG %s :Try !h for help", *c.par == '#'  ? c.par : c.nick);

	if (c.msg[0] != '!')
		return;

	switch (c.msg[1]) {
	case 'h':
		sendf(srv, "PRIVMSG %s :Usage: !h help, !v version, !o owner, !j join, !p part",
				*c.par == '#'  ? c.par : c.nick);
		break;
	case 'v':
		sendf(srv, "PRIVMSG %s :Bottler IRC-bot %s", *c.par == '#'  ? c.par : c.nick, VERSION);
		break;
	case 'o':
		sendf(srv, "PRIVMSG %s :My owner: %s", *c.par == '#'  ? c.par : c.nick, owner);
		break;
	}

	if (!owner || !!strcmp(c.mask, owner) || strlen(c.msg) < 4)
		return;

	switch (c.msg[1]) {
	case 'j':
		joinpart(srv, c.msg + 3, true);
		break;
	case 'p':
		joinpart(srv, c.msg + 3, false);
		break;
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

	memset(&c, 0, sizeof c);
	c.cmd = line;
	if (*c.cmd == ':') {
		c.nick = c.cmd+1;
		c.cmd = skip(c.nick, ' ');
		c.mask = skip(c.nick, '!');
	}
	c.par = skip(c.cmd, ' ');
	c.msg = skip(c.par, ':');
	trim(c.par);

	if (c.cmd && !strcmp("433", c.cmd)) {
		sendf(srv, "NICK %s-", nick);
		sendf(srv, "USER %s- 0 * :%s", nick, name);
	}

	if (c.cmd && c.nick && channels && !strcmp("MODE", c.cmd) && !strcmp(nick, c.nick))
		autojoin(srv);

	if (c.cmd && c.msg && !strcmp("PING", c.cmd))
		sendf(srv, "PONG %s", c.msg);

	if (c.nick && c.mask && c.cmd && c.par && c.msg) {
		corejobs(srv, c);
		urljobs(srv, c);
	}
}

void usage(void) {
	eprintf("usage: %s [options]\n"
			"\n"
			"-h    help\n"
			"-v    version\n"
			"\n"
			"-s <host>        server hostname\n"
			"-p <port>        server port\n"
			"-n <nick>        bot nickname\n"
			"-u <name>        bot username\n"
			"-o <owner>       bot owner\n"
			"-c <channels>    autojoin channels\n"
			, argv0);
}

int main(int argc, char **argv) {
	char buf[BUFSIZ];
	int fd = 0;
	FILE *srv = NULL;
	fd_set readfds;

	ARGBEGIN {
	case 'v':
		eprintf("%s-%s\n", argv0, VERSION);
	case 's':
		host = EARGF(usage());
		break;
	case 'p':
		port = EARGF(usage());
		break;
	case 'n':
		nick = EARGF(usage());
		break;
	case 'u':
		name = EARGF(usage());
		break;
	case 'o':
		owner = EARGF(usage());
		break;
	case 'c':
		channels = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;

	if (!port)
		port = "6667";
	if (!name)
		name = "Bottler IRC-bot " VERSION;
	if (!host)
		eprintf("you need to specify host\n");
	if (!nick)
		eprintf("you need to specify nick\n");

	while (1) {
		if (!srv) {
			close(fd);
			fd = dial(host, port);
			if (fd == -1) {
				fprintf(stderr, "Failed to connect to %s:%s.\n", host, port);
				fprintf(stderr, "Retrying in 15 seconds...\n");
				sleep(15);
				continue;
			} else {
				printf("connected to %s:%s\n", host, port);
			}

			srv = fdopen(fd, "r+");

			sendf(srv, "NICK %s", nick);
			sendf(srv, "USER %s 0 * :%s", nick, name);

			fflush(srv);
			setbuf(srv, NULL);
		}

		FD_ZERO(&readfds);
		FD_SET(fileno(srv), &readfds);

		if (select(fileno(srv) + 1, &readfds, 0, 0, &(struct timeval) {120, 0})) {
			if (FD_ISSET(fileno(srv), &readfds)) {
				if (!fgets(buf, sizeof buf, srv)) {
					fprintf(stderr, "Host closed connection.\n");
					fprintf(stderr, "Retrying in 15 seconds...\n");
					afclose(&srv);
					sleep(15);
					continue;
				}
				parseline(srv, buf);
			}
		} else {
			sendf(srv, "PING %s", host);
		}
	}

	sendf(srv, "QUIT :Terminating");
	afclose(&srv);
	close(fd);
	return EXIT_SUCCESS;
}
