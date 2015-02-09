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

char *argv0;

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
	printf("%.2d:%.2d:%.2d %s\n", tm->tm_hour, tm->tm_min, tm->tm_sec, buf);

	return fprintf(srv, "%s\r\n", buf);
}

void joinpart(FILE *srv, char *chan, bool join) {
	char *channel;

	if (chan[0] == '#') {
		sendf(srv, "%s %s", join ? "JOIN" : "PART", chan);
	} else {
		channel = emalloc(strlen(chan) + 2);
		sprintf(channel, "#%s", chan);
		sendf(srv, "%s %s", join ? "JOIN" : "PART", channel);
		free(channel);
	}
}

void autojoin(FILE *srv) {
	char *chanlist, *p;

	chanlist = estrdup(channels);

	while (chanlist) {
		p = strsep(&chanlist, " ");
		joinpart(srv, p, true);
	}

	free(chanlist);
}

void corejobs(FILE *srv, char *nick, char *mask, char *par, char *msg) {
	if (!strncmp(nick, msg, strlen(nick)))
		sendf(srv, "PRIVMSG %s :Try !h for help", *par == '#'  ? par : nick);

	if (msg[0] != '!')
		return;

	switch (msg[1]) {
	case 'h':
		sendf(srv, "PRIVMSG %s :Usage: !h help, !v version, !o owner, !j join, !p part",
				*par == '#'  ? par : nick);
		break;
	case 'v':
		sendf(srv, "PRIVMSG %s :Bottler IRC-bot %s", *par == '#'  ? par : nick, VERSION);
		break;
	case 'o':
		sendf(srv, "PRIVMSG %s :My owner: %s", *par == '#'  ? par : nick, owner);
		break;
	}

	if (!owner || !!strcmp(mask, owner) || strlen(msg) < 4)
		return;

	switch (msg[1]) {
	case 'j':
		joinpart(srv, msg + 3, true);
		break;
	case 'p':
		joinpart(srv, msg + 3, false);
		break;
	}
}

void urljobs(FILE *srv, char *par, char *msg) {
	char *url;
	char *title;

	if (par[0] == '#') {
		url = strcasestr(msg, "http://");
		if (!url)
			url = strcasestr(msg, "https://");
		if (!url)
			url = strcasestr(msg, "www.");
		if (url) {
			url = estrdup(url);
			url = strsep(&url, " \r\n");
			title = gettitle(url);
			if (title) {
				sendf(srv, "PRIVMSG %s :%s", par, title);
				free(title);
			}
			free(url);
		}
	}
}

void parseline(FILE *srv, char *line) {
	time_t t;
	struct tm *tm;
	char *nick, *mask, *cmd, *par, *msg;

	if (line[0] == ':') {
		line++;
		nick = strsep(&line, " ");
		mask = strsep(&nick, "!");
		cmd = strsep(&line, " ");
		par = strsep(&line, ":");
		msg = strsep(&line, "\r\n");
	} else {
		nick = NULL;
		mask = NULL;
		cmd = strsep(&line, " ");
		par = strsep(&line, ":");
		msg = strsep(&line, "\r\n");
	}
	if (par)
		par[strlen(par) - 1] = '\0';

	t = time(NULL);
	tm = localtime(&t);
	printf("%.2d:%.2d:%.2d n:%s m:%s c:%s p:%s m:%s\n",
			tm->tm_hour, tm->tm_min, tm->tm_sec, nick, mask, cmd, par, msg);

	if (cmd && !strcmp("433", cmd)) {
		sendf(srv, "NICK %s-", botnick);
		sendf(srv, "USER %s- 0 * :%s", botnick, botname);
	}

	if (cmd && channels && (!strcmp("376", cmd) || !strcmp("422", cmd)))
		autojoin(srv);

	if (cmd && msg && !strcmp("PING", cmd))
		sendf(srv, "PONG %s", msg);

	if (nick && mask && par && msg) {
		corejobs(srv, nick, mask, par, msg);
		urljobs(srv, par, msg);
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
		botnick = EARGF(usage());
		break;
	case 'u':
		botname = EARGF(usage());
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
	if (!botname)
		botname = "Bottler IRC-bot " VERSION;
	if (!host)
		eprintf("you need to specify host\n");
	if (!botnick)
		eprintf("you need to specify nick\n");

	while (true) {
		if (!srv) {
			close(fd);
			fd = dial(host, port);
			if (fd == -1) {
				fprintf(stderr, "Failed to connect to %s:%s.\n", host, port);
				fprintf(stderr, "Retrying in 120 seconds...\n");
				sleep(120);
				continue;
			} else {
				printf("connected to %s:%s\n", host, port);
			}

			srv = fdopen(fd, "r+");

			sendf(srv, "NICK %s", botnick);
			sendf(srv, "USER %s 0 * :%s", botnick, botname);

			fflush(srv);
			setbuf(srv, NULL);
		}

		FD_ZERO(&readfds);
		FD_SET(fileno(srv), &readfds);

		if (select(fileno(srv) + 1, &readfds, 0, 0, &(struct timeval) {120, 0})) {
			if (FD_ISSET(fileno(srv), &readfds)) {
				if (!fgets(buf, sizeof buf, srv)) {
					fprintf(stderr, "Host closed connection.\n");
					fprintf(stderr, "Retrying in 30 seconds...\n");
					afclose(&srv);
					sleep(30);
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
