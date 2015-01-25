#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <jansson.h>
#include <curl/curl.h>

#include "util.h"

#define BUFFER_SIZE  (1024 * 1024)  /* 1024 KB */

char *url = "http://www.w3.org/html/wg/drafts/html/master/entities.json";

struct write_result {
	char *data;
	int pos;
};

static size_t write_response(void *ptr, size_t size, size_t nmemb, void *stream) {
	struct write_result *result = (struct write_result *)stream;

	if (result->pos + size * nmemb >= BUFFER_SIZE - 1)
		eprintf("too small buffer\n");

	memcpy(result->data + result->pos, ptr, size * nmemb);
	result->pos += size * nmemb;

	return size * nmemb;
}

static char *request(const char *url) {
	CURL *curl;
	CURLcode status;
	char *data;
	long code;

	curl = curl_easy_init();
	data = malloc(BUFFER_SIZE);
	if (!curl || !data)
		return NULL;

	struct write_result write_result = {
		.data = data,
		.pos = 0
	};

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_result);

	status = curl_easy_perform(curl);
	if (status != 0)
		eprintf("unable to request data from %s:\n"
				"%s\n", url, curl_easy_strerror(status));

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	if(code != 200)
		eprintf("server responded with code %ld\n", code);

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	data[write_result.pos] = '\0';

	return data;
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

char *escapeformatspecifiers(const char *src) {
	int i;
	char *tmp = NULL;
	char *tmp2 = (char *) src;

	struct formatspecifiers {
		char *specifier;
		char *substitute;
	} fspecs[] = {
		{ "\\", "\\\\" },
		{ "\"", "\\\"" },
		{ "\n", "\\n" },
		{ NULL, NULL },
	};

	for (i = 0; fspecs[i].specifier; i++) {
		tmp = strrep(tmp2, fspecs[i].specifier, fspecs[i].substitute);
		if (i)
			free(tmp2);
		tmp2 = tmp;
	}

	return tmp2;
}

int main(void) {
	char *text;
	const char *key;
	size_t index;
	json_t *root;
	json_t *value;
	json_t *arrvalue;
	json_t *characters, *codepoints;
	json_error_t error;
	char *chartmp;

	text = request(url);
	if (!text)
		return 1;

	root = json_loads(text, 0, &error);
	free(text);

	if (!root)
		eprintf("line %d: %s\n", error.line, error.text);
	if (!json_is_object(root))
		eprintf("not an object\n");

	puts("#ifndef HTMLENTITIES_H");
	puts("#define HTMLENTITIES_H\n");
	puts("struct entity {");
	puts("\tchar *entity;");
	puts("\tchar *substitute;");
	puts("} entities[] = {");
	json_object_foreach(root, key, value) {
		characters = json_object_get(value, "characters");
		codepoints = json_object_get(value, "codepoints");
		if (!json_is_array(codepoints))
			eprintf("not an array");
		chartmp = escapeformatspecifiers(json_string_value(characters));
		printf("\t{ \"%s\", \"%s\" },\n", key, chartmp);
		json_array_foreach(codepoints, index, arrvalue) {
			printf("\t{ \"&#%.0f;\", \"%s\" },\n", json_number_value(arrvalue), chartmp);
		}
		free(chartmp);
	}
	puts("\t{ NULL, NULL },");
	puts("};\n");
	puts("#endif");

	json_decref(root);

	return 0;
}
