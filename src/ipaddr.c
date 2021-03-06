#include "ipaddr.h"
#include <stdint.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <stdio.h>

static bool addrInRange(const struct IPAddr test_addr, const struct IPAddr ref_addr, unsigned char ref_mask){
	if (test_addr.af != ref_addr.af){
		return false;
	}
	if (ref_mask == 0)
		return true;
	switch(test_addr.af){
	case AF_INET:{
		in_addr_t mask = -1; // -1 is guaranteed to be all ones
		if (ref_mask < 32)
			mask <<= (32 - ref_mask); // Network byte order is most significant byte left
		mask = ntohl(mask); // in_addr_t is in host byte order
		return ((test_addr.ipv4.s_addr & mask)
		        == (ref_addr.ipv4.s_addr & mask));
	}
	case AF_INET6:
		for(size_t i = 0; i < sizeof(test_addr.ipv6.s6_addr) && ref_mask > 0; ++i, ref_mask -= 8){
			if (ref_mask < 8) {
				unsigned char byte_mask = 255 << (8 - ref_mask);
				return ((ref_addr.ipv6.s6_addr[i] & byte_mask)
				        == (test_addr.ipv6.s6_addr[i] & byte_mask));
			}
			else if (ref_addr.ipv6.s6_addr[i] != test_addr.ipv6.s6_addr[i])
				return false;
		}
		return true;
	default:
		errno = EINVAL;
		return false;
	}
}

struct IPAddr addrFromAttr(struct ifaddrmsg const * msg, struct rtattr const * attr) {
	struct IPAddr addr = { .af = msg->ifa_family };
	switch (addr.af) {
	case AF_INET:
		addr.ipv4 = *((struct in_addr *) RTA_DATA(attr));
		break;
	case AF_INET6:
		addr.ipv6 = *((struct in6_addr *) RTA_DATA(attr));
		break;
	}
	return addr;
};

bool addrEqual(const struct IPAddr addr1, const struct IPAddr addr2){
	if (addr1.af != addr2.af)
		return false;
	switch (addr1.af){
	case AF_INET:
		return addr1.ipv4.s_addr == addr2.ipv4.s_addr;
	case AF_INET6:
		for (size_t i = 0; i < sizeof(addr1.ipv6.s6_addr); ++i){
			if (addr1.ipv6.s6_addr[i] != addr2.ipv6.s6_addr[i])
				return false;
		}
		return true;
	case AF_UNSPEC:
		return false;
	default:
		errno = EINVAL;
		return false;
	}
}

int printAddr(struct IPAddr const address){
	char ip_str[INET6_ADDRSTRLEN];
	if (!inet_ntop(address.af, &address, ip_str, sizeof(ip_str))){
		perror("Error converting IP to string");
		return -1;
	}

	if (puts(ip_str) != EOF) {
		return 0;
	} else {
		return -1;
	}
}

// Might use a different return code for invalid result, save checking/setting errno
bool addrIsPrivate(const struct IPAddr addr){
	struct IPAddr ref_addr = {.af=addr.af};	// Ensure address is initialized before use

	switch(addr.af){
	case AF_INET:
		ref_addr.ipv4.s_addr = ntohl(0x0A000000);	// 10.0.0.0
		if (addrInRange(addr, ref_addr, 8))
			return true;
		ref_addr.ipv4.s_addr = ntohl(0xAC100000);	// 172.16.0.0
		if (addrInRange(addr, ref_addr, 12))
			return true;
		ref_addr.ipv4.s_addr = ntohl(0xC0A80000);	// 192.168.0.0
		if (addrInRange(addr, ref_addr, 16))
			return true;
		return false;
	case AF_INET6:
		ref_addr.ipv6.s6_addr[0] = 0xFC;	// fc00::
		return addrInRange(addr, ref_addr, 7);
	default:
		errno = EINVAL;
		return false;
	}
}

bool addrIsLoopback(const struct IPAddr addr){
	struct IPAddr ref_addr = {.af=addr.af};	// Ensure address is initialized before use
	switch(addr.af){
	case AF_INET:
		ref_addr.ipv4.s_addr = ntohl(0x7F000000);	// 127.0.0.0
		return addrInRange(addr, ref_addr, 8);
	case AF_INET6:
		ref_addr.ipv6.s6_addr[15] = 0x01;	// ::1
		return addrInRange(addr, ref_addr, 128);
	default:
		errno = EINVAL;
		return false;
	}
}
