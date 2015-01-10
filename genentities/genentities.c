#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
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

int main(void) {
	char *text;
	const char *key;
	size_t index;
    json_t *root;
	json_t *value;
	json_t *arrvalue;
	json_t *characters, *codepoints;
    json_error_t error;

    text = request(url);
    if(!text)
        return 1;

    root = json_loads(text, 0, &error);
    free(text);

    if (!root)
        eprintf("error: on line %d: %s\n", error.line, error.text);
    if (!json_is_object(root))
        eprintf("error: root is not an object\n");

	puts("struct entity {");
	puts("\tstruct entity {");
	puts("\tchar *substitute;");
	puts("} entities[] = {");
	json_object_foreach(root, key, value) {
		characters = json_object_get(value, "characters");
		codepoints = json_object_get(value, "codepoints");
		if(!json_is_array(codepoints))
			puts("not an array");
		printf("\t{ \"%s\", \"%s\" },\n", key, json_string_value(characters));
		json_array_foreach(codepoints, index, arrvalue) {
			printf("\t{ \"&#%.0f;\", \"%s\" },\n", json_number_value(arrvalue), json_string_value(characters));
		}
	}
	puts("\t{ NULL, NULL },");
	puts("};");

    json_decref(root);
    return 0;
}
