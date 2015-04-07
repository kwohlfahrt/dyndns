#ifndef _MONITOR_H
#define _MONITOR_H

#include <sys/types.h>
#include <stdbool.h>
#include "filter.h"
#include "ipaddr.h"

struct MonitorState {
	ssize_t socket;
	char * buf;
	size_t buf_len;

	ssize_t nlmsg_len;
	struct nlmsghdr * nlh;

	size_t rtmsg_len;
	struct rtattr * rth;
};

bool initState(struct AddrFilter const filter, struct MonitorState * const state, size_t const buf_len);
struct IPAddr nextAddr(struct AddrFilter const filter, struct MonitorState * const state);

#endif /*_MONITOR_H*/
