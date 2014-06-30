#ifndef _IPADDR_H
#define _IPADDR_H

#include <netinet/in.h>
#include <stdbool.h>

struct IPAddr {
	union {
		struct in6_addr ipv6;
		struct in_addr ipv4;
	}; // First so doesn't need to be named
	unsigned char af;
};

bool addrIsPrivate(struct IPAddr const addr);
bool addrIsLoopback(struct IPAddr const addr);
bool addrInRange(struct IPAddr const test_addr, struct IPAddr const ref_addr, unsigned char ref_mask);
bool addrEqual(struct IPAddr const addr1, struct IPAddr const addr2);
int printAddr(struct IPAddr const addr);
#endif /*_IPADDR_H*/
