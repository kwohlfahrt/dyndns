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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <errno.h>

#include "monitor.h"
#include "filter.h"
#include "ipaddr.h"
#include "util.h"

struct Monitor {
	struct AddrFilter filter;
	char * buf;
	size_t buf_len;
	int epoll_fd;

	Updater_t updater;
};

struct EpollMonitor {
	enum EpollTag tag;
	ssize_t socket;
	struct Monitor data;
};

static ssize_t createSocket(){
	ssize_t sock;
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
		.nl_pid = 0 // kernel takes care of nl_pid if it is 0
	};

	if ((sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE)) == -1) {
		return -1;
	}

	if (fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK) == -1) {
		goto cleanup;
	}

	if ((bind(sock, (struct sockaddr *)&addr, sizeof(addr))) == -1) {
		goto cleanup;
	}

	return sock;
cleanup:
	close(sock);
	return -1;
}

static int requestAddr(struct AddrFilter const * filter, ssize_t const sock){
	unsigned char af =
		(filter->ipv4 && !filter->ipv6) ? AF_INET
		: (filter->ipv6 && !filter->ipv4) ? AF_INET6
		: AF_UNSPEC;

	struct {
		struct nlmsghdr nlh;
		struct ifaddrmsg ifa;
	} req = {
		.nlh = {
			.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg)),
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH,
			.nlmsg_type = RTM_GETADDR,
			.nlmsg_pid = getpid(),
		}, .ifa = {
			.ifa_family = af,
			// Does nothing as NLM_F_MATCH not implemented
			.ifa_index = filter->iface,
		},
	};

	if (send(sock, &req, req.nlh.nlmsg_len, 0) >= 0) {
		return 0;
	} else {
		return -1;
	}
}

Monitor_t createMonitor(struct AddrFilter const filter, size_t buf_len, int epoll_fd, Updater_t updater) {
	Monitor_t data = malloc(sizeof(*data));
	data->tag = EPOLL_MONITOR;
	data->socket = -1;

	struct Monitor * monitor = &data->data;
	monitor->updater = updater;
	monitor->epoll_fd = -1;

	data->socket = createSocket();
	if (data->socket == -1) goto cleanup;

	monitor->filter = filter;
	if (filter.ipv4) {
		enum rtnetlink_groups group = RTNLGRP_IPV4_IFADDR;
		if (setsockopt(data->socket, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &group, sizeof(group)) == -1) goto cleanup;
	}

	if (filter.ipv6) {
		enum rtnetlink_groups group = RTNLGRP_IPV6_IFADDR;
		if (setsockopt(data->socket, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &group, sizeof(group)) == -1) goto cleanup;
	}

	monitor->buf = malloc(buf_len);
	if (monitor->buf == NULL) goto cleanup;
	monitor->buf_len = buf_len;

	struct epoll_event event = {
		.events = EPOLLIN,
		.data = { .ptr = data },
	};
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, data->socket, &event) == -1) goto cleanup;
	monitor->epoll_fd = epoll_fd;

	if (requestAddr(&filter, data->socket) != 0) goto cleanup;

	return data;
cleanup:
	destroyMonitor(data);
	return NULL;
}

void destroyMonitor(Monitor_t data) {
	struct Monitor * monitor = &data->data;

	if (monitor->epoll_fd < 0) {
		epoll_ctl(monitor->epoll_fd, EPOLL_CTL_DEL, data->socket, NULL);
	}
	if (data->socket < 0) {
		close(data->socket);
	}
	if (monitor->buf != NULL) {
		monitor->buf_len = 0;
		free(monitor->buf);
		monitor->buf = NULL;
	}

	free(data);
	return;
}

int processMessage(Monitor_t data) {
	struct Monitor * monitor = &data->data;

	ssize_t len = recv(data->socket, monitor->buf, monitor->buf_len, MSG_TRUNC);
	if (len == -1) {
		// Error reading socket
		return -1;
	} else if (len == 0) {
		// Closed by kernel
		return -2;
	} else if ((size_t) len > monitor->buf_len) {
		free(monitor->buf);
		monitor->buf = malloc(len);
		monitor->buf_len = (size_t) len;

		// clear socket, else get EBUSY on requestAddr
		while (recv(data->socket, monitor->buf, monitor->buf_len, MSG_DONTWAIT) != -1) continue;
		if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;

		// May have missed data, re-request
		return requestAddr(&monitor->filter, data->socket);
	}

	struct nlmsghdr * nlh;
	size_t nlmsg_len;
	for (nlh = (struct nlmsghdr*) monitor->buf, nlmsg_len = monitor->buf_len;
	     NLMSG_OK(nlh, nlmsg_len) && nlh->nlmsg_type != NLMSG_DONE;
	     nlh = NLMSG_NEXT(nlh, nlmsg_len)) {
		switch (nlh->nlmsg_type) {
		case NLMSG_ERROR:
			errno = -((struct nlmsgerr *) NLMSG_DATA(nlh))->error;
			return  -1;
		case RTM_NEWADDR: {
			struct ifaddrmsg * ifa = (struct ifaddrmsg *) NLMSG_DATA(nlh);
			if (!filterMessage(&monitor->filter, ifa)) break;

			struct rtattr * rth;
			size_t rtmsg_len;
			for (rth = IFA_RTA(ifa), rtmsg_len = IFA_PAYLOAD(nlh);
			     RTA_OK(rth, rtmsg_len);
			     RTA_NEXT(rth, rtmsg_len)) {
				if (!filterAttr(&monitor->filter, ifa, rth)) continue;
				struct IPAddr addr = addrFromAttr(ifa, rth);
				int result = update(monitor->updater, addr);
				if (result != 0) return result;
			}
		}}
	}

	return 0;
}
