#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

#include "util.h"
#include "gettitle.h"
#include "htmlentities.h"

char *strrep(const char *str, const char *old, const char *new) {
	size_t oldlen, newlen, retlen;
	ptrdiff_t sharedlen;
	char *ret, *r;
	const char *p, *p2;

	oldlen = strlen(old);
	newlen = strlen(new);
	retlen = strlen(str);

	if (oldlen != newlen)
		for (p = str; (p2 = strstr(p, old)); p = p2 + oldlen)
			retlen += newlen - oldlen;

	ret = emalloc(retlen + 1);

	for (p = str, r = ret; (p2 = strstr(p, old)); p = p2 + oldlen) {
		sharedlen = p2 - p;
		memcpy(r, p, sharedlen);
		r += sharedlen;
		memcpy(r, new, newlen);
		r += newlen;
	}

	strcpy(r, p);

	return ret;
}

char *getxmlstr(char *s, char *t) {
	char *open, *close;

	open = emalloc(strlen(t) + 3);
	close = emalloc(strlen(t) + 4);
	sprintf(open, "<%s>", t);
	sprintf(close, "</%s>", t);

	if (strlen(s) < (strlen(open) + strlen(close) + 1))
		return NULL;

	s = strcasestr(s, open);
	if (s) {
		s += strlen(open);
		t = strcasestr(s, close);
		if (t) {
			while (isspace(t[-1]))
				t--;
			*t = '\0';
			while (isspace(*s))
				s++;
			s = strrep(s, "\n", "");
		} else {
			s = NULL;
		}
	}

	free(open);
	free(close);
	return s;
}

static char *replacehtmlentities(char *str) {
	int i;
	char *p, *p2;

	for (p = str; *p && *p != '&'; p++);
	if (*p == '&') {
		for (; *p && *p != ';' && !isspace(*p); p++);
		if (*p == ';') {
			for (i = 0, p2 = str; entities[i].entity; i++) {
				p = strrep(p2, entities[i].entity, entities[i].substitute);
				free(p2);
				p2 = p;
			}
			return p2;
		}
	}
	return str;
}

static size_t curlcallback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct htmldata *mem = (struct htmldata *) userp;

	mem->memory = erealloc(mem->memory, mem->size + realsize + 1);

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = '\0';

	return realsize;
}

char *gettitle(char *url) {
	char *title = NULL;
	CURL *curl;
	CURLcode res;
	struct htmldata data;

	data.memory = NULL;
	data.size = 0;

	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();

	// specify URL to get
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlcallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&data);
	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		fprintf(stderr, "curl_easy_perform(): %s\n", curl_easy_strerror(res));
	else
		title = getxmlstr(data.memory, "title");

	curl_easy_cleanup(curl);
	if (data.memory)
		free(data.memory);
	curl_global_cleanup();

	if (title)
		title = replacehtmlentities(title);

	return title;
}
