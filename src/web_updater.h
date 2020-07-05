#pragma once

#include "ipaddr.h"

typedef struct EpollData * Updater_t;

Updater_t createUpdater();
void destroyUpdater(Updater_t updater);
int webUpdate(struct IPAddr addr, void* data);
