#pragma once

typedef struct Updater * Updater_t;

#include <stdint.h>
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
	};
};

Updater_t createPrintUpdater();
int update(Updater_t updater, struct IPAddr addr);
int handleMessage(Updater_t updater, int fd, int32_t events);
int handleTimeout(Updater_t updater);

void destroyUpdater(Updater_t updater);
