#define _POSIX_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define MAXLINE 4096

char *host = "irc.quakenet.org";
char *port = "6667";
char *nick = "bottler";
char *name = "Botanic Bottler";

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
	char bufout[MAXLINE+1];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(bufout, sizeof bufout, fmt, ap);
	va_end(ap);
	strncat(bufout, "\r\n", sizeof bufout);

	printf("%s", bufout);
	return send(fd, bufout, strlen(bufout), 0);
}

int getfd(int fd, char *bufin) {
	int n;

	n = recv(fd, bufin, MAXLINE, 0);
	bufin[n] = '\0';
	if (n > 0)
		printf("%s", bufin);
	return n;
}

char *skip(char *s, char c) {
	while(*s != c && *s != '\0')
		s++;
	if(*s != '\0')
		*s++ = '\0';
	return s;
}

char *urltitle(char *url) {
	int n;
	int urlfd;
	static char bufurl[MAXLINE+1];
	char *hosturl;
	char *page;
	char *title;
	char *useragent = "Mozilla/5.0 (X11; Linux x86_64; rv:30.0) Gecko/20100101 Firefox/30.0";

	hosturl = strdup(url);
	page = skip(hosturl, '/');

	urlfd = dial(hosturl, "80");
	putfd(urlfd, "GET /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n",
			page, hosturl, useragent);
	free(hosturl);

	while (1) {
		n = getfd(urlfd, bufurl);
		if (n > 0) {
			title = strstr(bufurl, "<title>");
			if (title) {
				skip(title, '\n');
				title = skip(title, '>');
				skip(title, '<');
				return title;
			} else
				return NULL;
		}
	}
}

int main(int argc, char *argv[]) {
	int n;
	int fd;
	char bufin[MAXLINE+1];
	char *line;
	char *usr, *cmd, *par, *txt;
	char *url, *title;

	fd = dial(host, port);
	if (fd == -1) {
		fprintf(stderr, "failed to connect %s:%s\n", host, port);
		return EXIT_FAILURE;
	}

	putfd(fd, "NICK %s", nick);
	putfd(fd, "USER %s 0 * :%s", nick, name);

	while(1) {
		n = getfd(fd, bufin);
		if (n < 1)
			continue;
		for (line = strtok(bufin, "\n"); line; line = strtok(NULL, "\n")) {
			cmd = line;
			skip(cmd, '\r');
			usr = host;
			if (cmd[0] == ':') {
				usr = cmd+1;
				cmd = skip(usr, ' ');
				skip(usr, '!');
			}
			par = skip(cmd, ' ');
			txt = skip(par, ':');
			skip(par, ' ');

			if(!strcmp("PING", cmd)) {
				putfd(fd, "PONG %s", txt);
				continue;
			}

			/* commands */
			if (txt[0] == '!') {
				if (txt[1] == 'h')
					putfd(fd, "PRIVMSG %s :Usage: !h help, !p part, !j join",
							par[0] == '#' ? par : usr);
				else if (txt[1] == 'p' && par[0] == '#')
					putfd(fd, "PART %s", par);
				else if (txt[1] == 'j' && txt[3] == '#') {
					putfd(fd, "JOIN %s", txt+3);
					putfd(fd, "PRIVMSG %s :I was invited by %s. Try !h to see my commands.",
							txt+3, usr);
				}
			}

			/* fetch url titles */
			if (par[0] == '#') {
				if ((url = strstr(txt, "www.")) == 0)
					if ((url = strstr(txt, "http://")) != 0)
						url += 7;
			}
			if (url) {
				skip(url, ' ');
				if ((title = urltitle(url)) != 0)
					putfd(fd, "PRIVMSG %s :title:%s", par, title);
			}
		}

		if (!strcmp("1/0", txt) && par[0] == '#') {
			putfd(fd, "QUIT :division by zero");
			close(fd);
			return EXIT_SUCCESS;
		}
	}
}
