#include "ipaddr.h"
#include <stdbool.h>
#include <errno.h>
#include <sys/socket.h>
#include <stddef.h>
#include <arpa/inet.h>

#include <stdio.h>

// Could implement in terms of addrInRange, but this is shorter.
bool addrIsPrivate(const struct IPAddr addr){
	switch(addr.af){
	case AF_INET:
		// 10.0.0.0/8
		if ((htonl(addr.addr.ipv4.s_addr) & 0xFF000000) == 0x0A000000)
			return true;
		// 172.16.0.0/12
		if ((htonl(addr.addr.ipv4.s_addr) & 0xFFF00000) == 0xAC100000)
			return true;
		// 192.168.0.0/16
		if ((htonl(addr.addr.ipv4.s_addr) & 0xFFFF0000) == 0xC0A80000)
			return true;
		return false;
	case AF_INET6:
		// fc00::/7
		if ((addr.addr.ipv6.s6_addr[0] & 0xFE) == 0xFC)
			return true;
		return false;
	default:
		errno = EINVAL;
		return false;
	}
}

bool addrInRange(const struct IPAddr test_addr, const struct IPAddr ref_addr, unsigned char ref_mask){
	if (test_addr.af != ref_addr.af){
		errno = EINVAL;
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
		return ((test_addr.addr.ipv4.s_addr & mask)
		        == (ref_addr.addr.ipv4.s_addr & mask));
	}
	case AF_INET6:
		for(size_t i = 0; i < sizeof(test_addr.addr.ipv6.s6_addr) && ref_mask > 0; ++i, ref_mask -= 8){ 
			if (ref_mask < 8) {
				unsigned char byte_mask = 255 << (8 - ref_mask);
				return ((ref_addr.addr.ipv6.s6_addr[i] & byte_mask)
				        == (test_addr.addr.ipv6.s6_addr[i] & byte_mask));
			}
			else if (ref_addr.addr.ipv6.s6_addr[i] != test_addr.addr.ipv6.s6_addr[i])
				return false;
		}
		return true;
	default:
		errno = EINVAL;
		return false;
	}
}

bool addrEqual(const struct IPAddr addr1, const struct IPAddr addr2){
	if (addr1.af != addr2.af)
		return false;
	switch (addr1.af){
	case AF_INET:
		return addr1.addr.ipv4.s_addr == addr2.addr.ipv4.s_addr;
	case AF_INET6:
		for (size_t i = 0; i < sizeof(addr1.addr.ipv6.s6_addr); ++i){
			if (addr1.addr.ipv6.s6_addr[i] != addr2.addr.ipv6.s6_addr[i])
				return false;
		}
		return true;
	default:
		errno = EINVAL;
		return false;
	}
}

// Could also use `bool` return to match others in this file (but would require wrapper for webUpdate)
int printAddr(struct IPAddr const address){
	char ip_str[INET6_ADDRSTRLEN];
	if (!inet_ntop(address.af, &address.addr, ip_str, sizeof(ip_str))){
		perror("Error converting IP to string");
		return 1;
	}
	return !(puts(ip_str) >= 0);
}
