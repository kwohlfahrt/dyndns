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
#include <sys/epoll.h>
#include <sys/select.h>

#include "web_updater.h"
#include "ipaddr.h"
#include "filter.h"
#include "util.h"

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

static int socket_cb(CURL* handle, curl_socket_t socket, int what, void *cb_data, void * socket_data) {
	struct WebUpdater * updater = cb_data;
	// TODO: cb_data->fd needs to be set to socket

	if (what == CURL_POLL_REMOVE) {
		free(socket_data);
		return epoll_ctl(updater->epoll_fd, EPOLL_CTL_DEL, socket, NULL);
	} else {
		struct epoll_event ev = {
			.events = 0,
		};

		if (what == CURL_POLL_IN || what == CURL_POLL_INOUT) {
			ev.events |= EPOLLIN;
		}
		if (what == CURL_POLL_OUT || what == CURL_POLL_INOUT) {
			ev.events |= EPOLLOUT;
		}

		if (socket_data == NULL) {
			struct EpollData * data = malloc(sizeof(*data));
			data->tag = EPOLL_WEB_UPDATER;
			data->fd = socket;
			data->web_updater = updater;
			if (curl_multi_assign(updater->multi_handle, socket, data) != CURLM_OK) return -1;

			ev.data.ptr = data;
			return epoll_ctl(updater->epoll_fd, EPOLL_CTL_ADD, socket, &ev);
		} else {
			ev.data.ptr = socket_data;
			return epoll_ctl(updater->epoll_fd, EPOLL_CTL_MOD, socket, &ev);
		}
	};
}

static int timer_cb(CURLM* multi_handle, long timeout, void* cb_data) {
	struct WebUpdater * updater = cb_data;

	if (timeout == -1) {
		// Get rid of timeout
		*updater->timeout = -1;
	} else {
		*updater->timeout = timeout;
	}

	return 0;
}

Updater_t createWebUpdater(char const * template, int epoll_fd, int * timeout) {
	Updater_t data = malloc(sizeof(*data));
	data->tag = WEB_UPDATER;
	struct WebUpdater * updater = &data->web;
	updater->multi_handle = NULL;
	updater->handle = NULL;
	updater->url = NULL;
	updater->template = template;
	updater->timeout = timeout;
	updater->n_active = 0;
	updater->epoll_fd = epoll_fd;

	size_t url_len = strlen(template) + 1;
	char const * tag_pos = template;
	size_t tag_len = strlen(ip_tag);
	while ((tag_pos = strstr(tag_pos, ip_tag)) != NULL) {
		// Subtract the trailing NULL
		url_len += INET6_ADDRSTRLEN - 1;
		tag_pos += tag_len;
	}
	updater->url = malloc(url_len);
	if (updater->url == NULL) goto cleanup;
	updater->url_len = url_len;

	if ((updater->multi_handle = curl_multi_init()) == NULL) goto cleanup;
	if (curl_multi_setopt(updater->multi_handle, CURLMOPT_SOCKETDATA, updater) != CURLM_OK) goto cleanup;
	if (curl_multi_setopt(updater->multi_handle, CURLMOPT_SOCKETFUNCTION, socket_cb) != CURLM_OK) goto cleanup;
	if (curl_multi_setopt(updater->multi_handle, CURLMOPT_TIMERDATA, updater) != CURLM_OK) goto cleanup;
	if (curl_multi_setopt(updater->multi_handle, CURLMOPT_TIMERFUNCTION, timer_cb) != CURLM_OK) goto cleanup;

	if ((updater->handle = curl_easy_init()) == NULL) goto cleanup;
	return data;

cleanup:
	destroyWebUpdater(updater);
	free(data);
	return NULL;
};

void destroyWebUpdater(struct WebUpdater * updater) {
	if (updater->multi_handle != NULL) curl_multi_cleanup(updater->multi_handle);
	updater->multi_handle = NULL;
	if (updater->handle != NULL) curl_easy_cleanup(updater->handle);
	updater->handle = NULL;
	if (updater->url != NULL) free(updater->url);
	updater->url = NULL;
};

static int completeRequests(struct WebUpdater * updater, int fd, int events) {
	int prev_active = updater->n_active;
	if (curl_multi_socket_action(updater->multi_handle, fd, events, &updater->n_active) != CURLM_OK) return -1;
	if (prev_active > updater->n_active) {
		int nmsgs;
		for (CURLMsg * msg = curl_multi_info_read(updater->multi_handle, &nmsgs);
		     msg != NULL; msg = curl_multi_info_read(updater->multi_handle, &nmsgs)) {
			if (msg->msg != CURLMSG_DONE) {
				continue;
			}
			CURL* e = msg->easy_handle;
			CURLcode result = msg->data.result;
			curl_multi_remove_handle(updater->multi_handle, e);
			char * url = NULL;
			curl_easy_getinfo(e, CURLINFO_EFFECTIVE_URL, &url);
			if (result == CURLE_OK) {
				if (url != NULL) printf("Fetched: %s\n", url);
				return 0;
			} else if (result == CURLE_COULDNT_RESOLVE_HOST) {
				// TODO: DNS might not be available shortly after network comes up, so retry
				return -1;
			} else {
				if (url != NULL) printf("Failed to fetch: %s\n", url);
				return -1;
			}
		}
		return 0;
	}
	return 0;
}

int handleWebTimeout(struct WebUpdater * updater) {
	return completeRequests(updater, CURL_SOCKET_TIMEOUT, 0);
}

int handleWebMessage(struct WebUpdater * updater, int fd, int32_t events) {
	if (events & EPOLLIN) {
		events |= CURL_CSELECT_IN;
	}
	if (events & EPOLLOUT) {
		events |= CURL_CSELECT_OUT;
	}
	return completeRequests(updater, fd, events);
}

int webUpdate(struct WebUpdater * updater, struct IPAddr const addr){
	if (!templateUrl(addr, updater->template, updater->url, updater->url_len)) return -1;
	if (updater->n_active > 0) {
		curl_multi_remove_handle(updater->multi_handle, updater->handle);
	}
	if (curl_easy_setopt(updater->handle, CURLOPT_WRITEFUNCTION, discard) != CURLE_OK) return -1;
	if (curl_easy_setopt(updater->handle, CURLOPT_URL, updater->url) != CURLE_OK) return -1;
	if (curl_multi_add_handle(updater->multi_handle, updater->handle) != CURLM_OK) return -1;
	printf("Fetching address: %s\n", updater->url);

	if (curl_multi_socket_action(updater->multi_handle, CURL_SOCKET_TIMEOUT, 0, &updater->n_active) != CURLM_OK) return -1;

	return 0;
}
