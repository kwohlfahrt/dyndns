#pragma once

#define NELEMS(x) (sizeof(x) / sizeof(*x))

enum EpollTag {
	EPOLL_MONITOR,
	EPOLL_UPDATER,
};
