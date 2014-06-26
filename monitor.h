#ifndef _MONITOR_H
#define _MONITOR_H

#include "filter.h"
#include "dyndns.h"

int monitorAddrs(struct filter const filter, struct SharedAddr * dst);

#endif /*_MONITOR_H*/
