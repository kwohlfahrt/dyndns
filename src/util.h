#pragma once

#define NELEMS(x) (sizeof(x) / sizeof(*x))

enum EpollTag {
	TAG_MONITOR,
	TAG_UPDATER,
};
