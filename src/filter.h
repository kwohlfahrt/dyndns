#pragma once

#include <linux/rtnetlink.h>
#include <stdbool.h>
#include <stddef.h>

#include "ipaddr.h"

struct AddrFilter {
	unsigned int iface;

	bool allow_private;
	bool allow_temporary;

	bool ipv4;
	bool ipv6;
};

struct rtattr* filterMessage(struct AddrFilter const * filter, struct nlmsghdr const * nlh);
