#pragma once

#include <sys/types.h>
#include "filter.h"

typedef struct EpollData * Monitor_t;
Monitor_t createMonitor(struct AddrFilter const * filter, size_t buf_len, int epoll_fd);
void destroyMonitor(Monitor_t monitor);
