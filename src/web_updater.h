#pragma once

#include <curl/curl.h>
#include "ipaddr.h"

struct WebUpdater {
	CURLM* multi_handle;
	CURL* handle;

	char const* template;
	char* url;
	size_t url_len;

	int* timeout;
};

void destroyWebUpdater(struct WebUpdater * updater);
int webUpdate(struct WebUpdater * updater, struct IPAddr addr);

#include "updater.h"

Updater_t createWebUpdater(char const * template, int * timeout);
