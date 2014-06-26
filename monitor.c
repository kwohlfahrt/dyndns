#include <net/if.h>
#include <netinet/in.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_addr.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <bits/socket.h>
#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif /*SOL_NETLINK*/
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include "monitor.h"
#include "dyndns.h"
#include "filter.h"
#include "ipaddr.h"

// TODO: Make this (& filter) more versatile so can also use for route waiting
// Probably make monitorAddr(filter, sock) a blocking call that returns an IPAddr

static bool requestAddr(ssize_t sock, struct filter filter){
	struct {
		struct nlmsghdr nlh;
		struct ifaddrmsg ifa;
	} req = {
		.nlh = {
			.nlmsg_len= NLMSG_LENGTH(sizeof(struct ifaddrmsg)),
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

static ssize_t createMonitorSocket(struct filter const filter){
	ssize_t sock;
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
		// .nl_pid = 0 // kernel takes care of nl_pid if it is 0
	};
	struct timeval timeout = {.tv_sec = 5}; // TODO: Remove once interrupt safe.

	if ((sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE)) == -1) {
		return -1;
	}

	if ((bind(sock, (struct sockaddr *)&addr, sizeof(addr))) == -1) {
		goto cleanup;
	}

	if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&timeout, sizeof(timeout))){
		goto cleanup;
	}

	for (size_t i = 0; i < filter.num_af; i++) {
		if (setsockopt(sock, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
			(void *)&filter.af[i], sizeof(*filter.af)) == -1) {
			goto cleanup;
		}
	}
	return sock;

cleanup:
	close(sock);
	return -1;
}

static struct IPAddr processNlMsgs(const char * buf, ssize_t len, struct filter filter){
	struct IPAddr retval = {.af = AF_UNSPEC};

	for (struct nlmsghdr *nlh = (struct nlmsghdr *) buf;
	     NLMSG_OK(nlh, len) && (nlh->nlmsg_type != NLMSG_DONE);
	     nlh = NLMSG_NEXT(nlh, len)){
	     	switch (nlh->nlmsg_type) {
		case NLMSG_ERROR: {
			struct nlmsgerr * err = (struct nlmsgerr *) NLMSG_DATA(nlh);
			errno = -err->error;
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
					retval.addr.ipv4 = *((struct in_addr *) RTA_DATA(rth));
					break;
				case AF_INET6:
					retval.addr.ipv6 = *((struct in6_addr *) RTA_DATA(rth));
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

int monitorAddrs(struct filter const filter, struct SharedAddr * dst){
	char * buf = NULL;
	size_t buf_len = 1024;
	int retval = 0;
	ssize_t sock;

	if ((sock = createMonitorSocket(filter)) == -1)
		return -1; // sock will not be open

	if (!requestAddr(sock, filter)){
		retval = -1;
		goto cleanup;
	}

	struct IPAddr addr;
	ssize_t len;
	buf = (char *) malloc(buf_len);
	while ((len = recv(sock, buf, buf_len, MSG_TRUNC)) > 0){
		if ((size_t) len > buf_len){
			buf_len = (size_t) len;
			buf = (char *) realloc(buf, buf_len);
			// resynchronize (TODO clear socket?)
			if (!requestAddr(sock, filter)){
				retval = -1;
				goto cleanup;
			}
		}

		errno = 0;
		addr = processNlMsgs(buf, len, filter);
		if (addr.af == AF_UNSPEC){
			if (errno != 0) {
				retval = -1;
				goto cleanup;
			} else {
				// No interesting messages
				continue;
			}
		}

		pthread_mutex_lock(&dst->mutex);
		dst->addr = addr;
		pthread_mutex_unlock(&dst->mutex);
		kill(getppid(), SIGUSR1);
	}
	
	if (len == -1){
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			retval = 0;
		else
			retval = -1;
		goto cleanup;
	}
	
cleanup:
	close(sock);
	free(buf);
	return retval;
}
