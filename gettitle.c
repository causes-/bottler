#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <curl/curl.h>

#include "util.h"
#include "gettitle.h"

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

// taken from http://creativeandcritical.net/str-replace-c/
static char *replace_str2(const char *str, const char *old, const char *new) {
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

static char *replacehtmlentities(char *str) {
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
		{ "&comma;", "," },
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
