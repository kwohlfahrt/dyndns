#pragma once

#include <linux/rtnetlink.h>
#include <netinet/in.h>
#include <stdbool.h>

struct IPAddr {
	union {
		struct in6_addr ipv6;
		struct in_addr ipv4;
	}; // First so doesn't need to be named
	unsigned char af;
};

// Assumes valid msg->ifa_family
struct IPAddr addrFromAttr(struct ifaddrmsg const * msg, struct rtattr const * attr);
bool addrIsPrivate(struct IPAddr const addr);
bool addrIsLoopback(struct IPAddr const addr);
// AF_UNSPEC always compare unequal
bool addrEqual(struct IPAddr const addr1, struct IPAddr const addr2);
int printAddr(struct IPAddr const addr);
