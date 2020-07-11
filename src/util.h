#pragma once

#define NELEMS(x) (sizeof(x) / sizeof(*x))

#include "monitor.h"


enum EpollTag {
	EPOLL_MONITOR,
	EPOLL_WEB_UPDATER,
};

struct EpollData {
	enum EpollTag tag;
	int fd;
	// Use pointer types, can have >1 epoll per updater
	union {
		Monitor_t monitor;
		struct WebUpdater * web_updater;
	};
};
