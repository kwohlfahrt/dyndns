#ifndef _IPADDR_H
#define _IPADDR_H

#include <netinet/in.h>
#include <stdbool.h>

struct IPAddr {
	int af;
	union {
		struct in6_addr ipv6;
		struct in_addr ipv4;
	} addr;
};

bool addrIsPrivate(struct IPAddr const addr);
bool addrInRange(struct IPAddr const test_addr, struct IPAddr const ref_addr, unsigned char ref_mask);
bool addrEqual(struct IPAddr const addr1, struct IPAddr const addr2);
int printAddr(struct IPAddr const addr);
#endif /*_IPADDR_H*/
