#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>

#include "config.h"
#include "util.h"
#include "gettitle.h"

#define VERSION "0.2"

bool terminate = false;

void sighandler(int sig) {
	terminate = true;
	puts("Terminating...");
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
	}

	if (owner && !strcmp(c.mask, owner)) {
		if (!strncmp("!j", c.msg, 2) && strlen(c.msg) > 3)
			joinpart(srv, c.msg + 3, true);
		else if (!strncmp("!p", c.msg, 2) &&  strlen(c.msg) > 3)
			joinpart(srv, c.msg + 3, false);
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

	if (c.cmd && c.nick && channels && !strcmp("MODE", c.cmd) && !strcmp(nick, c.nick))
		autojoin(srv);

	if (c.cmd && c.msg && !strcmp("PING", c.cmd))
		sendf(srv, "PONG %s", c.msg);

	if (c.nick && c.mask && c.cmd && c.par && c.msg) {
		corejobs(srv, c);
		urljobs(srv, c);
	}
}

int main(int argc, char **argv) {
	int i;
	char buf[BUFSIZ];
	int fd = 0;
	FILE *srv = NULL;
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
					"-s <host>        server hostname\n"
					"-p <port>        server port\n"
					"-n <nick>        bot nickname\n"
					"-u <name>        bot username\n"
					"-o <owner>       bot owner\n"
					"-c <channels>    autojoin channels\n"
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
		else if (!strcmp("-c", argv[i]))
			channels = argv[++i];
	}

	if (!port)
		port = "6667";
	if (!name)
		name = "Bottler IRC-bot " VERSION;
	if (!host)
		eprintf("you need to specify host\n");
	if (!nick)
		eprintf("you need to specify nick\n");

	signal(SIGINT, sighandler);

	while (!terminate) {
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
					fclose(srv);
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
	fclose(srv);
	close(fd);
}
