#ifndef _MONITOR_H
#define _MONITOR_H

#include <sys/types.h>
#include <stdbool.h>
#include "filter.h"
#include "ipaddr.h"

ssize_t createAddrSocket(struct AddrFilter const filter);
bool requestAddr(struct AddrFilter const filter, ssize_t const sock);
struct IPAddr nextAddr(struct AddrFilter const filter, ssize_t const sock);

#endif /*_MONITOR_H*/
