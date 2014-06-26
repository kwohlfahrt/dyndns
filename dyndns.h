#ifndef _DYNDNS_H
#define _DYNDNS_H

#include "ipaddr.h"

struct SharedAddr {
	struct IPAddr addr;
	pthread_mutex_t mutex;
};
#endif /*_DYNDNS_H*/
