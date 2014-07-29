#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define BUF 4096

struct cmd {
	char *nick;
	char *host;
	char *cmd;
	char *par;
	char *msg;
};

struct cfg {
	char *server;
	char *port;
	char *nick;
	char *name;
	char *admin;
	char *config;
};

static sig_atomic_t reload;
static sig_atomic_t quit;

void sighandler(int sig) {
	switch (sig) {
	case SIGHUP:
		reload = 1;
		break;
	case SIGINT:
	case SIGTERM:
		quit = 1;
		break;
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
	void *p = malloc(size);
	if (!p)
		eprintf("out of memory\n");
	return p;
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
	return ++s;
}

char *skipstr(char *s, char *t) {
	char *p;
	p = strstr(s, t);
	if (p) {
		p += strlen(t);
		if (*p == '\0' || strlen(p) < 3)
			p = NULL;
	}
	return p;
}

char *splitline(char **s) {
	char *b = *s;
	char *e = *s;
	while (*e != '\n' && *e != '\r' && *e != '\0')
		e++;
	if (b == e)
		return NULL;
	if (*e != '\0') {
		while (*e == '\n' || *e == '\r' || *e == ' ')
			*e++ = '\0';
	}
	*s = e;
	return b;
}

int loadconfig(struct cfg *cfg) {
	FILE *fp;
	char *line = NULL;
	char *p;
	size_t n = 0;

	fp = fopen(cfg->config, "r");
	if (fp == NULL) {
		fprintf(stderr, "can't open %s\n", cfg->config);
		return 0;
	}
	while (getline(&line, &n, fp) != -1) {
		line = splitline(&line);
		if (!line || line[0] == '#')
			continue;
		p = skipstr(line, "server:");
		if (p) {
			free(cfg->server);
			cfg->server = estrdup(p);
		}
		p = skipstr(line, "port:");
		if (p) {
			free(cfg->port);
			cfg->port = estrdup(p);
		}
		p = skipstr(line, "nick:");
		if (p) {
			free(cfg->nick);
			cfg->nick = estrdup(p);
		}
		p = skipstr(line, "name:");
		if (p) {
			free(cfg->name);
			cfg->name = estrdup(p);
		}
		p = skipstr(line, "admin:");
		if (p) {
			free(cfg->admin);
			cfg->admin = estrdup(p);
		}
	}
	if (!cfg->port)
		cfg->port = "6667";
	if (!cfg->admin)
		cfg->admin = "nobody";
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
	int len;
	char *bufout = emalloc(BUF+1);
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(bufout, BUF, fmt, ap);
	va_end(ap);
	strcat(bufout, "\r\n");

	printf("send:%s", bufout);
	len = send(fd, bufout, strlen(bufout), 0);
	free(bufout);
	return len;
}

int getfd(int fd, char *bufin) {
	int len;
	len = recv(fd, bufin, BUF, MSG_DONTWAIT);
	if (len > 0) {
		bufin[len] = '\0';
		printf("%s", bufin);
	}
	return len;
}

void parseline(char *line, struct cmd *cmd, struct cfg cfg) {
	if (!line || !*line)
		return;
	cmd->cmd = line;
	cmd->nick = cfg.server;
	cmd->host = "";
	if (!strncmp(cmd->cmd, ":", 1)) {
		cmd->nick = cmd->cmd+1;
		cmd->cmd = splitstr(cmd->nick, ' ');
		cmd->host = splitstr(cmd->nick, '!');
	}
	cmd->par = splitstr(cmd->cmd, ' ');
	cmd->msg = splitstr(cmd->par, ':');
}

char *xmlstr(char *s, char *t) {
	char *b = emalloc(strlen(t)+3);
	char *e = emalloc(strlen(t)+4);

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

char *httpgetxml(char *url, char *xmltag) {
	int len = 0;
	int fd;
	char *bufurl;
	char *host, *page, *value = NULL;

	if (!url || !xmltag || !*url || !*xmltag)
		return NULL;
	host = url;
	page = splitstr(host, '/');
	fd = contohost(host, "80");
	if (fd == -1)
		return NULL;
	putfd(fd, "GET http://%s/%s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:30.0) Gecko/20100101 Firefox/30.0\r\n"
			, host, page, host);

	bufurl = emalloc(BUF+1);
	sleep(1);
	while (getfd(fd, bufurl) && !value)
		value = xmlstr(bufurl, xmltag);
	free(bufurl);

	close(fd);
	return value;
}

void coreopts(struct cmd cmd, struct cfg cfg, int fd) {
	if (!strncmp("!h", cmd.msg, 2)) {
		putfd(fd, "PRIVMSG %s :Usage: !h help, !p part, !j join, !m hostmask, !o owner",
				!strncmp("#", cmd.par, 1) ? cmd.par : cmd.nick);
	} else if (!strncmp("!o", cmd.msg, 2)) {
		putfd(fd, "PRIVMSG %s :My owner: %s",
				!strncmp("#", cmd.par, 1) ? cmd.par : cmd.nick,
				cfg.admin);
	} else if (!strncmp("!m", cmd.msg, 2)) {
		putfd(fd, "PRIVMSG %s :%s",
				!strncmp("#", cmd.par, 1) ? cmd.par : cmd.nick,
				cmd.host);
	} else if (!strncmp("!j #", cmd.msg, 4)) {
		putfd(fd, "JOIN %s", cmd.msg+3);
		putfd(fd, "PRIVMSG %s :I was invited by %s. Try !h to see my commands.", cmd.msg+3, cmd.nick);
	} else if (!strncmp("!p", cmd.msg, 2)) {
		if (!strncmp("#", cmd.par, 1)) {
			putfd(fd, "PART %s", cmd.par);
		}
	}
}

void adminopts(struct cmd cmd, struct cfg cfg, int fd) {
	if (!strcmp(cmd.host, cfg.admin) && !!strncmp("#", cmd.par, 1)) {
		if (!strncmp("!r", cmd.msg, 2)) {
			reload = 1;
			putfd(fd, "PRIVMSG %s :Reloading %s", cmd.nick, cfg.config);
		} else if (!strncmp("!q", cmd.msg, 2)) {
			quit = 1;
		} else if (!strncmp("!h", cmd.msg, 2)) {
			putfd(fd, "PRIVMSG %s :Usage: !q quit, !r reload", cmd.nick);
		}
	}
}

int urlopts(struct cmd cmd, int fd) {
	char *url, *title;
	if (!strncmp("#", cmd.par, 1)) {
		url = skipstr(cmd.msg, "http://");
		if (!url)
			url = skipstr(cmd.msg, "https://");
		if (!url)
			url = strstr(cmd.msg, "www.");
		if (url) {
			url = estrdup(url);
			splitstr(url, ' ');
			title = httpgetxml(url, "title");
			if (title) {
				putfd(fd, "PRIVMSG %s :%s", cmd.par, title);
				free(title);
				return 1;
			}
			free(url);
		}
	}
	return 0;
}

int main(int argc, char *argv[]) {
	int len = 0;
	int fd = -1;
	char *bufin;
	char *lines;
	char *line;
	char *user;
	char *defpath;
	struct cmd cmd;
	struct cfg cfg;

	memset(&cfg, 0, sizeof cfg);

	for (len = 1; len < argc; len++) {
		if (argv[len+1] == NULL || argv[len+1][0] == '-') {
			eprintf("usage: %s [options]\n"
					"\n"
					"-h               help\n"
					"-f <file>        config file\n"
					"-s <host>        server hostname\n"
					"-p <port>        server port\n"
					"-n <nick>        bot nickname\n"
					"-N <name>        bot name\n"
					"-m <hostmask>    admin hostmask\n"
					, argv[0]);
		} else if (!strcmp("-f", argv[len])) {
			cfg.config = argv[++len];
			loadconfig(&cfg);
		} else if (!strcmp("-s", argv[len])) {
			cfg.server = argv[++len];
		} else if (!strcmp("-p", argv[len])) {
			cfg.port = argv[++len];
		} else if (!strcmp("-n", argv[len])) {
			cfg.nick  = argv[++len];
		} else if (!strcmp("-N", argv[len])) {
			cfg.name  = argv[++len];
		} else if (!strcmp("-m", argv[len])) {
			cfg.admin = argv[++len];
		}
	}

	signal(SIGHUP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	if (!cfg.config) {
		user = getenv("USER");
		if (user) {
			defpath = "/home/%s/.bottler.conf";
			cfg.config = emalloc(strlen(user)+strlen(defpath));
			sprintf(cfg.config, defpath, user);
			loadconfig(&cfg);
		}
	}
	if (!cfg.server || !cfg.nick || !cfg.name)
		eprintf("You need to specify server, nick and name\n");

	printf("server:%s:%s\nnick:%s\nname:%s\nadmin:%s\nconfig:%s\n",
			cfg.server, cfg.port, cfg.nick, cfg.name, cfg.admin, cfg.config);

	fd = contohost(cfg.server, cfg.port);
	if (fd == -1)
		eprintf("failed to connect %s:%s\n", cfg.server, cfg.port);
	else
		printf("connected to %s:%s\n", cfg.server, cfg.port);

	putfd(fd, "NICK %s", cfg.nick);
	putfd(fd, "USER %s 0 * :%s", cfg.nick, cfg.name);

	bufin = emalloc(BUF+1);
	while(quit == 0) {
		len = getfd(fd, bufin);
		if (len > 0) {
			lines = bufin;
			while ((line = splitline(&lines)) != NULL) {
				if (reload)
					loadconfig(&cfg);

				parseline(line, &cmd, cfg);

				if(!strcmp("PING", cmd.cmd))
					putfd(fd, "PONG %s", cmd.msg);

				coreopts(cmd, cfg, fd);
				adminopts(cmd, cfg, fd);
				urlopts(cmd, fd);
			}
		}
	}

	putfd(fd, "QUIT");
	close(fd);
	return EXIT_SUCCESS;
}
