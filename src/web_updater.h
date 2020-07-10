#pragma once

#include <curl/curl.h>
#include <sys/epoll.h>
#include "ipaddr.h"

struct WebUpdater {
	CURLM* multi_handle;
	CURL* handle;
	int n_active;

	int epoll_fd;

	char const* template;
	char* url;
	size_t url_len;

	int* timeout;
};

void destroyWebUpdater(struct WebUpdater * updater);
int webUpdate(struct WebUpdater * updater, struct IPAddr addr);
int handleWebMessage(struct WebUpdater * updater, int fd, int32_t events);
int handleWebTimeout(struct WebUpdater * updater);

#include "updater.h"

Updater_t createWebUpdater(char const * template, int epoll_fd, int * timeout);
