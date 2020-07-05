#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>

#include <string.h>
#ifndef HAVE_strlcpy
#include "strlcpy.h"
#define HAVE_strlcpy
#endif

#include <curl/curl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/inotify.h>
#include <sys/select.h>

#include "web_updater.h"
#include "ipaddr.h"
#include "filter.h"
#include "util.h"

struct Updater {
	CURLM* multi_handle;
	CURL* handle;

	char const* template;
	char* url;
	size_t url_len;

	int* timeout;
};

struct EpollData {
	enum EpollTag tag;
	struct Updater updater;
};

char const * const ip_tag = "<ipaddr>";

bool templateUrl(struct IPAddr address, char const * src, char * dst, size_t max_size){
	const size_t tag_len = strlen(ip_tag);

	char * write_pos = dst;
	const char * read_pos = src;
	const char * tag_pos = src;

	while ((tag_pos = strstr(read_pos, ip_tag)) != NULL){
		size_t copy_len = tag_pos - read_pos;
		if (copy_len > max_size) {
			errno = ENOSPC;
			return false;
		}
		memcpy(write_pos, read_pos, copy_len);
		write_pos += copy_len;
		max_size -= copy_len;

		if (inet_ntop(address.af, &address, write_pos, max_size) == NULL) {
			return false;
		}
		size_t ip_len = strlen(write_pos);
		write_pos += ip_len;
		max_size -= ip_len;

		read_pos = tag_pos + tag_len;
	}

	if (strlcpy(write_pos, read_pos, max_size) >= max_size){
		errno = ENOSPC;
		return false;
	}

	return true;
}

static size_t discard(__attribute__((unused)) char *ptr,
                      size_t size, size_t nmemb,
		      __attribute__((unused)) void *userdata){
	return size * nmemb;
}

static int socket_cb(CURL* handle, curl_socket_t s, int what, void *cb_data, void * socketp) {
	struct EpollData * data = (struct EpollData *) cb_data;

	return 0;
}

static int timer_cb(CURLM* multi_handle, long timeout, void* cb_data) {
	struct EpollData * data = (struct EpollData *) cb_data;

	if (timeout == -1) {
	}

	return 0;
}

Updater_t createUpdater(char const * template, int * timeout) {
	struct EpollData * data = malloc(sizeof(*data));
	data->tag = TAG_UPDATER;

	struct Updater * updater = &data->updater;
	updater->multi_handle = NULL;
	updater->handle = NULL;
	updater->url = NULL;
	updater->template = template;
	updater->timeout = timeout;

	size_t url_len = strlen(template) + 1;
	char const * tag_pos = template;
	while ((tag_pos = strstr(tag_pos, ip_tag)) != NULL) {
		// Subtract the trailing NULL
		url_len += INET6_ADDRSTRLEN - 1;
	}
	updater->url = malloc(url_len);

	if ((updater->multi_handle = curl_multi_init()) == NULL) goto cleanup;
	if (curl_multi_setopt(updater->multi_handle, CURLMOPT_SOCKETDATA, updater) != CURLM_OK) goto cleanup;
	if (curl_multi_setopt(updater->multi_handle, CURLMOPT_SOCKETFUNCTION, socket_cb) != CURLM_OK) goto cleanup;
	if (curl_multi_setopt(updater->multi_handle, CURLMOPT_TIMERDATA, updater) != CURLM_OK) goto cleanup;
	if (curl_multi_setopt(updater->multi_handle, CURLMOPT_TIMERFUNCTION, timer_cb) != CURLM_OK) goto cleanup;

	if ((updater->handle = curl_easy_init()) == NULL) goto cleanup;
	return data;

cleanup:
	destroyUpdater(data);
	return NULL;
};

void destroyUpdater(Updater_t data) {
	struct Updater * updater = &data->updater;

	if (updater->multi_handle != NULL) curl_multi_cleanup(updater->multi_handle);
	updater->multi_handle = NULL;
	if (updater->handle != NULL) curl_easy_cleanup(updater->handle);
	updater->handle = NULL;
	if (updater->url != NULL) free(updater->url);
	updater->url = NULL;
};

int webUpdate(struct IPAddr const addr, void* data){
	struct Updater * updater = (struct Updater *) data;

	if (!templateUrl(addr, updater->template, updater->url, updater->url_len)) return -1;
	if (curl_easy_setopt(updater->handle, CURLOPT_WRITEFUNCTION, discard) != CURLE_OK) return -1;
	if (curl_easy_setopt(updater->handle, CURLOPT_URL, updater->url) != CURLE_OK) return -1;
	if (curl_multi_add_handle(updater->multi_handle, updater->handle) != CURLM_OK) return -1;

	int n_active;
	if (curl_multi_socket_action(updater->multi_handle, CURL_SOCKET_TIMEOUT, 0, &n_active) != CURLM_OK) return -1;

	// If interface is recently brought up, this will fail with CURLE_COULDNT_RESOLVE_HOST
	// because NetworkManager hasn't set any DNS servers, so we will wait for one.
	if (retval == CURLE_COULDNT_RESOLVE_HOST){
	}

	return 0;
}
