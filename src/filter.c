#include "filter.h"
#include "ipaddr.h"
#include <stdio.h>

struct rtattr* filterMessage(struct AddrFilter const * filter, struct nlmsghdr const * nlh){
	struct ifaddrmsg * ifa = (struct ifaddrmsg *) NLMSG_DATA(nlh);

	if ((ifa->ifa_family == AF_INET6 && !filter->ipv6)
	    || (ifa->ifa_family == AF_INET && !filter->ipv4)) return NULL;
	if (ifa->ifa_index != filter->iface) return NULL;
	if (ifa->ifa_scope != RT_SCOPE_UNIVERSE && ifa->ifa_scope != RT_SCOPE_SITE) return NULL;

	uint32_t flags = ifa->ifa_flags;
	struct rtattr* addr_attr = NULL;

	struct rtattr* rth;
	size_t rtmsg_len;
	for (rth = IFA_RTA(ifa), rtmsg_len = IFA_PAYLOAD(nlh);
	     RTA_OK(rth, rtmsg_len);
	     rth = RTA_NEXT(rth, rtmsg_len)) {
		switch (rth->rta_type) {
		case IFA_ADDRESS:
			addr_attr = rth;
			break;
		case IFA_FLAGS:
			flags = *(uint32_t *) RTA_DATA(rth);
			break;
		}
	}
	if ((flags & IFA_F_DEPRECATED) || ((flags & IFA_F_TEMPORARY) && !filter->allow_temporary)) return NULL;
	struct IPAddr addr = addrFromAttr(ifa, addr_attr);
	if (addrIsPrivate(addr) && !filter->allow_private) return NULL;

	return addr_attr;
}
