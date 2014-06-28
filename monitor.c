#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_addr.h>
#include <sys/socket.h>
#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif /*SOL_NETLINK*/
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>

#include "monitor.h"
#include "filter.h"
#include "ipaddr.h"

static ssize_t createNlSocket(enum rtnetlink_groups const * const groups, size_t num_groups){
	ssize_t sock;
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
		// .nl_pid = 0 // kernel takes care of nl_pid if it is 0
	};
	struct timeval timeout = {.tv_sec = 30}; // TODO: Remove once interrupt safe.

	if ((sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE)) == -1) {
		return -1;
	}

	if ((bind(sock, (struct sockaddr *)&addr, sizeof(addr))) == -1) {
		goto cleanup;
	}

	if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&timeout, sizeof(timeout))){
		goto cleanup;
	}

	for (size_t i = 0; i < num_groups; ++i) {
		if (setsockopt(sock, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
			(void *)&groups[i], sizeof(groups[i])) == -1)
			goto cleanup;
	}

	return sock;
cleanup:
	close(sock);
	return -1;
}

ssize_t createAddrSocket(struct AddrFilter const filter){
	return createNlSocket(filter.af, filter.num_af);
}

ssize_t createRouteSocket(void){
	enum rtnetlink_groups grps[2] = {RTNLGRP_IPV4_ROUTE, RTNLGRP_IPV6_ROUTE};
	return createNlSocket(grps, sizeof(grps) / sizeof(grps[0]));
}

bool requestAddr(struct AddrFilter const filter, ssize_t const sock){
	struct {
		struct nlmsghdr nlh;
		struct ifaddrmsg ifa;
	} req = {
		.nlh = {
			.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg)),
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
			.nlmsg_type = RTM_GETADDR,
			.nlmsg_pid = getpid(),
		}, .ifa = {
			.ifa_family = 0, // Request all, filter later.
			.ifa_index = filter.iface, // Does nothing as NLM_F_MATCH not implemented
		},
	};
	
	if (send(sock, &req, req.nlh.nlmsg_len, 0) < 0){
		return false;
	}

	return true;
}

bool requestRoutes(struct RouteFilter const filter, ssize_t const sock){
	for (size_t i = 0; i < filter.num_addrs; ++i){
		struct IPAddr addr = filter.addrs[i];
		struct {
			struct nlmsghdr nlh;
			struct rtmsg rtm;
			struct rtattr rta;
			union {
				struct in_addr ipv4;
				struct in6_addr ipv6;
			} data;
		} req = {
			.nlh = {
				.nlmsg_flags = NLM_F_REQUEST,
				.nlmsg_type = RTM_GETROUTE,
				.nlmsg_pid = getpid(),
			}, .rtm = {
				.rtm_family = addr.af,
				.rtm_dst_len = 32,
			}, .rta = {
				.rta_type = RTA_DST,
			},
		};
		switch(addr.af){
			case AF_INET:
				req.rta.rta_len = RTA_LENGTH(sizeof(req.data.ipv4));
				req.data.ipv4 = addr.ipv4;
				break;
			case AF_INET6:
				req.rta.rta_len = RTA_LENGTH(sizeof(req.data.ipv6));
				req.data.ipv6 = addr.ipv6;
				break;
			default:
				errno = EINVAL;
				return false;
		}
		req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(req.rtm) + RTA_SPACE(req.rta.rta_len));
		// This seems to work with multiple messages, but I remember getting EBUSY if messages are sent before responses are read...
		// If this comes up might have to send one message with multiple nlmsghdrs
		// Would also avoid excessive send()s...
		if (send(sock, &req, req.nlh.nlmsg_len, 0) < 0){
			return false;
		}
	}

	return true;
}

static bool matchRoute(char const * buf, ssize_t len, struct RouteFilter const filter){
	for (struct nlmsghdr *nlh = (struct nlmsghdr *) buf;
	     NLMSG_OK(nlh, len) && (nlh->nlmsg_type != NLMSG_DONE);
	     nlh = NLMSG_NEXT(nlh, len)){
		switch (nlh->nlmsg_type) {
		case NLMSG_ERROR: {
			struct nlmsgerr * err = (struct nlmsgerr *) NLMSG_DATA(nlh);
			errno = -err->error;
			return false;
		}
		case RTM_NEWROUTE: {
			struct rtmsg *rtm = (struct rtmsg *) NLMSG_DATA(nlh);

			if (rtm->rtm_dst_len == 0){
				for (int i = 0; i < filter.num_addrs; ++i){
					if(filter.addrs[i].af == rtm->rtm_family){
						return true;
					}
				}
				continue;
			}

			struct rtattr *rta;
			unsigned int rtl;
			for (rta = RTM_RTA(rtm), rtl = RTM_PAYLOAD(nlh);
			     rtl && RTA_OK(rta, rtl);
			     rta = RTA_NEXT(rta, rtl)){ //RTA_NEXT modifies rtl
				if (rta->rta_type != RTA_DST)
					continue;

				struct IPAddr address;
				switch (rtm->rtm_family) {
				case AF_INET:
					address.ipv4 = *((struct in_addr *) RTA_DATA(rta));
					break;
				case AF_INET6:
					address.ipv6 = *((struct in6_addr *) RTA_DATA(rta));
					break;
				default:
					continue;
				}
				address.af = rtm->rtm_family;
				for (int i = 0; i < filter.num_addrs; ++i){
					if (addrInRange(filter.addrs[i], address, rtm->rtm_dst_len)){
						return true;
					}
				}
			}
			break;
		}
		}
	}
	return false;
}

bool waitRoute(struct RouteFilter const filter, ssize_t const sock){
	size_t buf_len = 1024;
	char * buf = (char *) malloc(buf_len);

	bool retval = false;
	ssize_t len;
	while (true){
		len = recv(sock, buf, buf_len, MSG_TRUNC);
		if (len == 0) {
			break;
		} else if (len < 0) {
			break;
		} else if ((size_t) len > buf_len) {
			// Reallocate with sufficient size
			buf_len = (size_t) len;
			buf = (char *) realloc(buf, buf_len);

			// Resynchronize
			// clear socket, else get EBUSY
			while (recv(sock, buf, buf_len, MSG_DONTWAIT) != -1)
				continue;
			if (errno != EAGAIN && errno != EWOULDBLOCK)
				break;
			if (!requestRoutes(filter, sock))
				break;
		} else {
			errno = 0;
			retval = matchRoute(buf, len, filter);
			if (retval == true || errno != 0)
				// Something interesting happened, return it.
				break;
		}
	}

	free(buf);
	return retval;
}

// Returns AF_MAX if error, AF_UNSPEC if none matching
static struct IPAddr matchAddr(char const * buf, ssize_t len, struct AddrFilter const filter){
	struct IPAddr retval = {.af = AF_UNSPEC};

	for (struct nlmsghdr *nlh = (struct nlmsghdr *) buf;
	     NLMSG_OK(nlh, len) && (nlh->nlmsg_type != NLMSG_DONE);
	     nlh = NLMSG_NEXT(nlh, len)){
	     	switch (nlh->nlmsg_type) {
		case NLMSG_ERROR: {
			struct nlmsgerr * err = (struct nlmsgerr *) NLMSG_DATA(nlh);
			errno = -err->error;
			retval.af = AF_MAX;
			return retval;
		}
		case RTM_NEWADDR: {
			struct ifaddrmsg *ifa = (struct ifaddrmsg *) NLMSG_DATA(nlh);
			if (!checkFilterAf(filter, ifa->ifa_family))
				continue;
			// Until NLM_F_MATCH implemented
			if (ifa->ifa_index != filter.iface)
				continue;
			
			if (ifa->ifa_scope != RT_SCOPE_UNIVERSE
			    && ifa->ifa_scope != RT_SCOPE_SITE)
				continue;
			
			{
			struct rtattr *rth;
			unsigned int rtl;
			for (rth = IFA_RTA(ifa), rtl = IFA_PAYLOAD(nlh);
			     rtl && RTA_OK(rth, rtl);
			     rth = RTA_NEXT(rth, rtl)){ //RTA_NEXT modifies RTL
				if (rth->rta_type != IFA_ADDRESS)
					continue;
				
				switch (ifa->ifa_family) {
				case AF_INET:
					retval.ipv4 = *((struct in_addr *) RTA_DATA(rth));
					break;
				case AF_INET6:
					retval.ipv6 = *((struct in6_addr *) RTA_DATA(rth));
					break;
				default:
					continue;
				}
				retval.af = ifa->ifa_family; 

				if (!filter.allow_private && addrIsPrivate(retval))
					continue;
			}
			}
			break;
		}
		}
	}
	return retval;
}

// Returns AF_MAX if error, AF_UNSPEC if no more addrs (socket closed)
struct IPAddr nextAddr(struct AddrFilter const filter, ssize_t const sock){
	size_t buf_len = 1024;
	char * buf = (char *) malloc(buf_len);

	struct IPAddr addr;
	ssize_t len;
	while (true){
		len = recv(sock, buf, buf_len, MSG_TRUNC);
		if (len == 0) {
			addr.af = AF_UNSPEC;
			break;
		}
		if (len < 0) {
			addr.af = AF_MAX;
			break;
		} else if ((size_t) len > buf_len){
			// Reallocate with sufficient size
			buf_len = (size_t) len;
			buf = (char *) realloc(buf, buf_len);

			// Resynchronize
			// clear socket, else get EBUSY
			while (recv(sock, buf, buf_len, MSG_DONTWAIT) != -1)
				continue;
			if (errno != EAGAIN && errno != EWOULDBLOCK)
				break;
			if (!requestAddr(filter, sock)){
				addr.af = AF_MAX;
				break;
			}
		} else {
			addr = matchAddr(buf, len, filter);
			if (addr.af != AF_UNSPEC)
				// Something interesting happened, return it.
				break;
		}
	}
	
	free(buf);
	return addr;
}
