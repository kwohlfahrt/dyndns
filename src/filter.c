#include "filter.h"
#include "ipaddr.h"

bool filterMessage(struct AddrFilter const * filter, struct ifaddrmsg const * msg){
	return ((msg->ifa_family == AF_INET6 && filter->ipv6)
	        || (msg->ifa_family == AF_INET && filter->ipv4))
	       && msg->ifa_index == filter->iface
	       && (msg->ifa_scope == RT_SCOPE_UNIVERSE
	           || msg->ifa_scope == RT_SCOPE_SITE)
	       && !(msg->ifa_flags & IFA_F_DEPRECATED);
}

bool filterAttr(struct AddrFilter const * filter,
		struct ifaddrmsg const * msg,
		struct rtattr const * attr){
	if (attr->rta_type != IFA_ADDRESS)
		return false;
	struct IPAddr addr = addrFromAttr(msg, attr);
	return filter->allow_private || !addrIsPrivate(addr);
}
