#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <jansson.h>
#include <curl/curl.h>

#define BUFFER_SIZE  (1024 * 1024)  /* 1024 KB */

char *url = "http://www.w3.org/html/wg/drafts/html/master/entities.json";

void eprintf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

struct write_result {
	char *data;
	int pos;
};

static size_t write_response(void *ptr, size_t size, size_t nmemb, void *stream) {
	struct write_result *result = (struct write_result *)stream;

	if(result->pos + size * nmemb >= BUFFER_SIZE - 1)
		eprintf("error: too small buffer\n");

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
		eprintf("error: unable to request data from %s:\n"
				"%s\n", url, curl_easy_strerror(status));

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	if(code != 200)
		eprintf("error: server responded with code %ld\n", code);

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	data[write_result.pos] = '\0';

	return data;
}

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
		 * * words we have already iterated through with strstr above,
		 * * and thus we know how many times we need to call it - then we
		 * * can avoid the final (potentially lengthy) call to strstr,
		 * * which we already know is going to return NULL, by
		 * * decrementing and checking count.
		 * */
		if (!samesize && !count--)
			break;
		/* Otherwise i.e. when the old and new strings are the same
		 * * length, and we don't know how many times to call strstr,
		 * * we must check for a NULL return here (we check it in any
		 * * event, to avoid further conditions, and because there's
		 * * no harm done with the check even when the old and new
		 * * strings are different lengths).
		 * */
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

char *escapeformatspecifiers(const char *src) {
	int i;
	char *tmp = NULL;
	char *tmp2 = src;

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
		tmp = replace_str2(tmp2, fspecs[i].specifier, fspecs[i].substitute);
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
	if(!text)
		return 1;

	root = json_loads(text, 0, &error);
	free(text);

	if (!root)
		eprintf("error: on line %d: %s\n", error.line, error.text);
	if (!json_is_object(root))
		eprintf("error: root is not an object\n");

	puts("#ifndef HTMLENTITIES_H");
	puts("#define HTMLENTITIES_H\n");
	puts("struct entity {");
	puts("\tchar *entity;");
	puts("\tchar *substitute;");
	puts("} entities[] = {");
	json_object_foreach(root, key, value) {
		characters = json_object_get(value, "characters");
		codepoints = json_object_get(value, "codepoints");
		if(!json_is_array(codepoints))
			puts("not an array");
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
