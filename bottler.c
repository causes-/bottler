#define _POSIX_SOURCE

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
	char bufout[BUFSIZ+1];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(bufout, sizeof bufout, fmt, ap);
	va_end(ap);
	strncat(bufout, "\r\n", sizeof bufout);

	printf("send:%s", bufout);
	return send(fd, bufout, strlen(bufout), 0);
}

int getfd(int fd, char *bufin) {
	int n;

	n = recv(fd, bufin, BUFSIZ, 0);
	if (n != -1)
		bufin[n] = '\0';
	if (n > 0)
		printf("recv:%s", bufin);
	return n;
}

char *urltitle(char *url) {
	int n;
	int fd;
	static char bufurl[BUFSIZ+1];
	char *host;
	char *page;
	char *useragent = "Mozilla/5.0 (X11; Linux x86_64; rv:30.0) Gecko/20100101 Firefox/30.0";
	char *title;

	host = strstr(url, "http://");
	if (host)
		host += 7;
	else
		host = url;
	page = skip(host, '/');

	fd = dial(host, "80");
	if (fd == -1)
		return NULL;

	putfd(fd, "GET http://%s/%s/ HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n",
			host, page, host, useragent);

	n = getfd(fd, bufurl);
	if (n > 0) {
		title = strstr(bufurl, "<title>");
		if (title) {
			title = skip(title, '>');
			skip(title, '<');
			close(fd);
			return title;
		}
	}

	close(fd);
	return NULL;
}

int main(int argc, char *argv[]) {
	int n;
	int fd;
	char bufin[BUFSIZ+1];
	char *line;
	char *usr, *cmd, *chan, *msg;
	char *url, *title;

	fd = dial(host, port);
	if (fd == -1)
		eprintf("failed to connect %s:%s\n", host, port);

	putfd(fd, "NICK %s", nick);
	putfd(fd, "USER %s 0 * :%s", nick, name);

	while(1) {
		n = getfd(fd, bufin);
		if (n < 1)
			continue;
		for (line = strtok(bufin, "\n"); line; line = strtok(NULL, "\n")) {
			/* parse received line */
			cmd = line;
			skip(cmd, '\r');
			usr = host;
			if (cmd[0] == ':') {
				usr = cmd+1;
				cmd = skip(usr, ' ');
				skip(usr, '!');
			}
			chan = skip(cmd, ' ');
			msg = skip(chan, ':');
			skip(chan, ' ');

			if(!strcmp("PING", cmd)) {
				putfd(fd, "PONG %s", msg);
				continue;
			}

			if (msg[0] == '!') {
				/* bot commands */
				if (msg[1] == 'p' && chan[0] == '#')
					putfd(fd, "PART %s", chan);
				else if (msg[1] == 'j' && msg[3] == '#') {
					putfd(fd, "JOIN %s", msg+3);
					putfd(fd, "PRIVMSG %s :I was invited by %s. Try !h to see my commands.",
							msg+3, usr);
				} else {
					putfd(fd, "PRIVMSG %s :Usage: !h help, !p part, !j join",
							chan[0] == '#' ? chan : usr);
				}
			} else if (chan[0] == '#') {
				/* channel messages */
				url = strstr(msg, "www.");
				if (!url)
					url = strstr(msg, "http://");
				if (url) {
					skip(url, ' ');	
					title = urltitle(url);
					if (title)
						putfd(fd, "PRIVMSG %s :%s", chan, title);
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
