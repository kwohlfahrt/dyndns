#ifndef _FILTER_H
#define _FILTER_H

#include <linux/rtnetlink.h>
#include <stdbool.h>
#include <stddef.h>

// Only AF_INET and AF_INET6
#define DYNDNS_FILTER_MAX_AF 2

struct AddrFilter {
	unsigned int iface;
	enum rtnetlink_groups af[DYNDNS_FILTER_MAX_AF];
	size_t num_af;
	bool allow_private;
};

struct RouteFilter {
	unsigned char num_addrs;
	struct IPAddr * addrs;
};

enum rtnetlink_groups afToRtnl(int af);
bool checkFilterAf(struct AddrFilter filter, int af);
bool addFilterAf(struct AddrFilter *filter, int af);

#endif /*_FILTER_H*/
