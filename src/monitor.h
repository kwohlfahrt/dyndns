#pragma once

#include <stddef.h>
#include <stdint.h>

#include "filter.h"
#include "updater.h"

typedef struct Monitor * Monitor_t;
Monitor_t createMonitor(struct AddrFilter const filter, size_t buf_len, int epoll_fd, Updater_t updater);
int processMessage(Monitor_t monitor, int fd, int32_t events);
void destroyMonitor(Monitor_t monitor);
