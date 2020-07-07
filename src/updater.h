#pragma once

typedef struct EpollUpdater * Updater_t;

#include <sys/epoll.h>
#include "util.h"
#include "ipaddr.h"
#include "web_updater.h"

struct PrintUpdater {};

enum UpdaterType {
	PRINT_UPDATER,
	WEB_UPDATER,
};

struct Updater {
	enum UpdaterType tag;
	union {
		struct WebUpdater web;
		struct PrintUpdater print;
	} data;
};

struct EpollUpdater {
	enum EpollTag tag;
	int fd;
	struct Updater data;
};

Updater_t createPrintUpdater();
int update(Updater_t updater, struct IPAddr addr);
int handleMessage(Updater_t updater, struct epoll_event * ev);
int handleTimeout(Updater_t updater);

void destroyUpdater(Updater_t updater);
