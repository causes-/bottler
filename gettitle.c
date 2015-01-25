#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <curl/curl.h>

#include "util.h"
#include "gettitle.h"
#include "htmlentities.h"

static char *getxmlstr(char *s, char *t) {
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

static size_t curlcallback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct htmldata *mem = (struct htmldata *) userp;

	mem->memory = erealloc(mem->memory, mem->size + realsize + 1);

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}

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

static char *replacehtmlentities(char *str) {
	int i;
	char *tmp = NULL;
	char *tmp2 = str;

	for (i = 0; entities[i].entity; i++) {
		tmp = strrep(tmp2, entities[i].entity, entities[i].substitute);
		if (i)
			free(tmp2);
		tmp2 = tmp;
	}

	return tmp2;
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

	if (title)
		title = replacehtmlentities(title);

	return title;
}
