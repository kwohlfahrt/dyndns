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
		.nl_pid = 0 // kernel takes care of nl_pid if it is 0
	};

	if ((sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE)) == -1) {
		return -1;
	}

	if ((bind(sock, (struct sockaddr *)&addr, sizeof(addr))) == -1) {
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

ssize_t nextMessage(struct AddrFilter const filter, ssize_t const socket,
                    char * * const buf, size_t * const buf_len){
	ssize_t len = recv(socket, *buf, *buf_len, MSG_TRUNC);
	if (len <= 0 ) {
		// Error
		return len;
	} else if ((size_t) len > *buf_len){
		// Reallocate with sufficient size
		*buf_len = (size_t) len;
		free(*buf);
		*buf = malloc(*buf_len);

		// clear socket, else get EBUSY on requestAddr
		while (recv(socket, *buf, *buf_len, MSG_DONTWAIT) != -1)
			continue;
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			return -1;

		if (!requestAddr(filter, socket))
			return -1;

		return nextMessage(filter, socket, buf, buf_len);
	} else {
		return len;
	}
}

bool initState(struct AddrFilter const filter, struct MonitorState * const state,
               size_t const buf_len){
	state->socket = createAddrSocket(filter);
	if (state->socket == -1)
		return false;
	if (!requestAddr(filter, state->socket)){
		close(state->socket);
		return false;
	}
	state->buf = malloc(buf_len);
	if (state->buf == NULL){
		close(state->socket);
		return false;
	}
	state->buf_len = buf_len;

	state->nlmsg_len = 0;
	state->nlh = NULL;
	return true;
}

bool filterIfAddrMsg(struct ifaddrmsg const ifa, struct AddrFilter const filter){
	return checkFilterAf(filter, ifa.ifa_family)
	       && ifa.ifa_index == filter.iface
	       && (ifa.ifa_scope == RT_SCOPE_UNIVERSE || ifa.ifa_scope == RT_SCOPE_SITE)
	       && !(ifa.ifa_flags & IFA_F_DEPRECATED);
}

// Returns AF_MAX (with errno set) if error, AF_UNSPEC if no more addrs (socket closed)
struct IPAddr nextAddr(struct AddrFilter const filter, struct MonitorState * const state){
	// NLMSG_OK checks length first, so safe to call with state->nlh == NULL iff
	// state->nlmsg_len < (int) sizeof(struct nlmsghdr)
	if (NLMSG_OK(state->nlh, state->nlmsg_len) && (state->nlh->nlmsg_type != NLMSG_DONE)){
		struct nlmsghdr * nlh = state->nlh;
		state->nlh = NLMSG_NEXT(state->nlh, state->nlmsg_len);
		switch(nlh->nlmsg_type){
		case NLMSG_ERROR:
			errno = -((struct nlmsgerr *) NLMSG_DATA(nlh))->error;
			struct IPAddr addr = {.af = AF_MAX};
			return addr;
		case RTM_NEWADDR: {
			struct ifaddrmsg * ifa = (struct ifaddrmsg *) NLMSG_DATA(nlh);
			if (!filterIfAddrMsg(*ifa, filter))
				return nextAddr(filter, state);
			{
			struct rtattr * rth;
			size_t rtmsg_len;
			for (rth = IFA_RTA(ifa), rtmsg_len = IFA_PAYLOAD(nlh);
			     RTA_OK(rth, rtmsg_len); RTA_NEXT(rth, rtmsg_len)){
				if (rth->rta_type != IFA_ADDRESS)
					continue;
				// family checked in filterIfAddrMsg, so always valid.
				struct IPAddr addr = {.af = ifa->ifa_family};
				switch (ifa->ifa_family) {
				case AF_INET:
					addr.ipv4 = *((struct in_addr *) RTA_DATA(rth));
					break;
				case AF_INET6:
					addr.ipv6 = *((struct in6_addr *) RTA_DATA(rth));
					break;
				}
				if (addrIsPrivate(addr) && !filter.allow_private)
					return nextAddr(filter, state);
				else
					return addr;
			}
			}
			// Recieved RTM_NEWADDR without any address.
			errno = EBADMSG;
			struct IPAddr addr = {.af = AF_MAX};
			return addr;
		}
		default:
			return nextAddr(filter, state);
		}
	} else {
		state->nlmsg_len = nextMessage(filter, state->socket, &state->buf, &state->buf_len);
		if (state->nlmsg_len == 0) {
			// Socket closed by kernel
			struct IPAddr addr = {.af = AF_UNSPEC};
			return addr;
		} else if (state->nlmsg_len < 0) {
			// Socket error
			struct IPAddr addr = {.af = AF_MAX};
			return addr;
		} else  {
			state->nlh = (struct nlmsghdr *) state->buf;
			return nextAddr(filter, state);
		}
	}
}
