#pragma once

#include <sys/types.h>
#include "filter.h"
#include "updater.h"

typedef struct EpollMonitor * Monitor_t;
Monitor_t createMonitor(struct AddrFilter const filter, size_t buf_len, int epoll_fd, Updater_t updater);
int processMessage(Monitor_t epoll_monitor);
void destroyMonitor(Monitor_t epoll_monitor);
