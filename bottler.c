#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "config.h"

#define VERSION "0.1"

struct command {
	char *nick;
	char *mask;
	char *cmd;
	char *par;
	char *msg;
};

struct htmldata {
	char *memory;
	size_t size;
};

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
	void *r;

	r = realloc(p, size);
	if (!r)
		eprintf("out of memory\n");
	return r;
}

void *estrdup(void *p) {
	void *r;

	r = strdup(p);
	if (!r)
		eprintf("out of memory\n");
	return r;
}

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

char *getxmlstr(char *s, char *t) {
	char *b = emalloc(strlen(t) + 3);
	char *e = emalloc(strlen(t) + 4);

	sprintf(b, "<%s>", t);
	sprintf(e, "</%s>", t);

	if (strlen(s) < (strlen(b)+strlen(e)+1))
		return NULL;

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

size_t curlcallback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct htmldata *mem = (struct htmldata *) userp;

	mem->memory = erealloc(mem->memory, mem->size + realsize + 1);

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}

char *gettitle(char *url) {
	char *title = NULL;
	CURL *curl_handle;
	CURLcode res;

	struct htmldata data;

	data.memory = malloc(1);
	data.size = 0;

	curl_global_init(CURL_GLOBAL_ALL);
	curl_handle = curl_easy_init();

	/* specify URL to get */ 
	curl_easy_setopt(curl_handle, CURLOPT_URL, url);
	/* send all data to this function  */ 
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, curlcallback);
	/* we pass our 'data' struct to the callback function */ 
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&data);
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	res = curl_easy_perform(curl_handle);

	if (res != CURLE_OK)
		fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
	else
		title = getxmlstr(data.memory, "title");

	curl_easy_cleanup(curl_handle);
	if (data.memory)
		free(data.memory);
	curl_global_cleanup();

	return title;
}

// taken from http://creativeandcritical.net/str-replace-c/
char *replace_str2(const char *str, const char *old, const char *new) {
	char *ret, *r;
	const char *p, *q;
	size_t oldlen = strlen(old);
	size_t count, retlen, newlen = strlen(new);
	int samesize = (oldlen == newlen);

	if (!samesize) {
		for (count = 0, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen)
			count++;
		/* This is undefined if p - str > PTRDIFF_MAX */
		retlen = p - str + strlen(p) + count * (newlen - oldlen);
	} else
		retlen = strlen(str);

	if ((ret = malloc(retlen + 1)) == NULL)
		return NULL;

	r = ret, p = str;
	while (1) {
		/* If the old and new strings are different lengths - in other
		 * words we have already iterated through with strstr above,
		 * and thus we know how many times we need to call it - then we
		 * can avoid the final (potentially lengthy) call to strstr,
		 * which we already know is going to return NULL, by
		 * decrementing and checking count.
		 */
		if (!samesize && !count--)
			break;
		/* Otherwise i.e. when the old and new strings are the same
		 * length, and we don't know how many times to call strstr,
		 * we must check for a NULL return here (we check it in any
		 * event, to avoid further conditions, and because there's
		 * no harm done with the check even when the old and new
		 * strings are different lengths).
		 */
		if ((q = strstr(p, old)) == NULL)
			break;
		/* This is undefined if q - p > PTRDIFF_MAX */
		ptrdiff_t l = q - p;
		memcpy(r, p, l);
		r += l;
		memcpy(r, new, newlen);
		r += newlen;
		p = q + oldlen;
	}
	strcpy(r, p);

	return ret;
}

char *replacehtmlentities(char *str) {
	int i;
	char *tmp = NULL;
	char *tmp2 = str;

	struct entity {
		char *entity;
		char *substitute;
	} entities[] = {
		{ "&nbsp;", " " },
		{ "&auml;", "ä" },
		{ "&Auml;", "Ä" },
		{ "&ouml;", "ö" },
		{ "&Ouml;", "Ö" },
		{ "&aring;", "å" },
		{ "&Aring;", "Å" },
		{ "&excl;", "!" },
		{ "&quest;", "?" },
		{ "&amp;", "&" },
		{ "&num;", "#" },
		{ "&quot;", "\"" },
		{ "&apos;", "'" },
		{ "&#39;", "'" },
		{ "&percnt;", "%" },
		{ "&lpar;", "(" },
		{ "&rpar;", ")" },
		{ "&lt;", "<" },
		{ "&gt;", ">" },
		{ "&ast;", "*" },
		{ "&plus;", "+" },
		{ "&comma;	", "," },
		{ "&period;", "." },
		{ "&colon;", ":" },
		{ "&semi;", ";" },
		{ "&equals;", "=" },
		{ "&sol;", "/" },
		{ "&bsol;", "\\" },
		{ "&commat;", "@" },
		{ "&lbrack;", "[" },
		{ "&rbrack;", "]" },
		{ "&lbrace;", "{" },
		{ "&rbrace;", "}" },
		{ "&sect;", "§" },
		{ "&copy;", "©" },
		{ "&reg;", "®" },
		{ "&amp;", "&" },
		{ "&dollar;", "$" },
		{ "&euro;", "€" },
		{ "&trade;", "™" },
		{ "&ndash;", "–" },
		{ NULL, NULL },
	};

	for (i = 0; entities[i].entity; i++) {
		tmp = replace_str2(tmp2, entities[i].entity, entities[i].substitute);
		if (i)
			free(tmp2);
		tmp2 = tmp;
	}

	return tmp2;
}

void urljobs(FILE *srv, struct command c) {
	char *url, *title;
	if (!strncmp("#", c.par, 1)) {
		url = c.msg;
		if (!strncmp(url, "http://", 7) ||
				!strncmp(url, "https://", 8) ||
				!strncmp(url, "www.", 4)) {
			url = estrdup(url);
			skip(url, ' ');
			trim(url);
			title = gettitle(url);
			if (title) {
				title = replacehtmlentities(title);
				sendf(srv, "PRIVMSG %s :%s", c.par, title);
				free(title);
			}
			free(url);
		}
	}
}

void corejobs(FILE *srv, struct command c) {
	char *channel;

	if (!strncmp("!h", c.msg, 2)) {
		sendf(srv, "PRIVMSG %s :Usage: !h help, !j join, !p part, !o owner",
				c.par[0] == '#'  ? c.par : c.nick);
	} else if (!strncmp("!o", c.msg, 2)) {
		sendf(srv, "PRIVMSG %s :My owner: %s",
				c.par[0] == '#'  ? c.par : c.nick,
				owner);
	} else if (!strcmp(c.mask, owner)) {
		if (!strncmp("!j", c.msg, 2)) {
			if (c.msg[3] == '#')
				sendf(srv, "JOIN %s", c.msg+3);
			else {
				channel = emalloc(strlen(c.msg+3) + 1);
				sprintf(channel, "#%s", c.msg+3);
				sendf(srv, "JOIN %s", channel);
				free(channel);
			}
		} else if (!strcmp("!p", c.msg)) {
			if (c.par[0] == '#')
				sendf(srv, "PART %s", c.par);
		} else if (!strncmp("!p ", c.msg, 3)) {
			if (c.msg[3] == '#')
				sendf(srv, "PART %s", c.msg+3);
			else {
				channel = emalloc(strlen(c.msg+3) + 1);
				sprintf(channel, "#%s", c.msg+3);
				sendf(srv, "PART %s", channel);
				free(channel);
			}
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
