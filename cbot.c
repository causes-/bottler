#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "config.h"

#define VERSION "0.1"

void eprintf(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

char *skip(char *s, char c) {
	while(*s != c && *s != '\0')
		s++;
	if(*s != '\0')
		*s++ = '\0';
	return s;
}

int dial(const char *host, const char *port) {
	int fd;
	struct addrinfo hints, *res, *r;

	res = NULL;

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

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	printf("<%s", buf);

	return fprintf(srv, "%s\r\n", buf);
}

bool parseline(FILE *srv, char *line) {
	char *nick, *mask, *cmd, *par, *msg;

	if (!line || !*line)
		return false;

	printf(">%s", line);

	cmd = line;
	nick = host;
	mask = NULL;
	if (*cmd == ':') {
		nick = cmd+1;
		cmd = skip(nick, ' ');
		mask = skip(nick, '!');
	}
	par = skip(cmd, ' ');
	msg = skip(par, ':');
	par[strlen(par)] = '\0';

	if (!strcmp("PING", cmd))
		sendf(srv, "PONG %s", msg);

	return true;
}

int main(int argc, char **argv) {
	int i;
	char buf[BUFSIZ];
	int fd;
	fd_set readfds;
	FILE *srv;

	for (i = 1; i < argc; i++) {
		if (!strcmp("-v", argv[i]))
			eprintf("%s-%s\n", argv[0], VERSION);
		else if (argv[i+1] == NULL || argv[i+1][0] == '-')
			eprintf("usage: %s [options]\n"
					"\n"
					"-h    help\n"
					"-v    version\n"
					"\n"
					"-s <host>       server hostname\n"
					"-p <port>       server port\n"
					"-n <nick>       bot nickname\n"
					"-u <name>       bot username\n"
					"-c <channel>    channel to join\n"
					, argv[0]);
		else if (!strcmp("-s", argv[i]))
			host = argv[++i];
		else if (!strcmp("-p", argv[i]))
			port = argv[++i];
		else if (!strcmp("-n", argv[i]))
			nick = argv[++i];
		else if (!strcmp("-u", argv[i]))
			name = argv[++i];
		else if (!strcmp("-c", argv[i]))
			channel = argv[++i];
	}

	if (!host || !port)
		eprintf("you need to specify host and port");
	if (!nick || !name)
		eprintf("you need to specify nick and name");

	fd = dial(host, port);
	if (fd == -1)
		eprintf("failed to connect to %s:%s\n", host, port);
	else
		printf("connected to %s:%s\n", host, port);

	srv = fdopen(fd, "r+");

	sendf(srv, "NICK %s", nick);
	sendf(srv, "USER %s 0 * :%s", nick, name);
	if (channel)
		sendf(srv, "JOIN %s", channel);

	fflush(srv);
	setbuf(srv, NULL);

	while (1) {
		FD_ZERO(&readfds);
		FD_SET(fileno(srv), &readfds);

		if (select(fileno(srv) + 1, &readfds, 0, 0, 0)) {
			if (FD_ISSET(fileno(srv), &readfds)) {
				if (!fgets(buf, sizeof buf, srv))
					eprintf("host closed connection\n");
				parseline(srv, buf);
			}
		}
	}
}
