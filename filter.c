#include "filter.h"

#include <sys/socket.h>
#include <errno.h>

enum rtnetlink_groups afToRtnl(int af){
	switch (af){
	case AF_INET:
		return RTNLGRP_IPV4_IFADDR;
	case AF_INET6:
		return RTNLGRP_IPV6_IFADDR;
	default:
		return RTNLGRP_NONE;
	}
};

bool checkFilterAf(struct filter filter, int af){
	if (filter.af == NULL){
		errno = EFAULT;
		return false;
	}

	enum rtnetlink_groups grp = afToRtnl(af);
	for (size_t i = 0; i < filter.num_af; i++)
		if (filter.af[i] == grp)
			return true;
	return false;
};

int addFilterAf(struct filter *filter, int af){
	if (filter == NULL){
		errno = EFAULT;
		return -1;
	}
		
	if (checkFilterAf(*filter, af))
		return 0;
	if (filter->num_af >= sizeof(filter->af)){
		errno = ENOSPC;
		return -1;
	}
	filter->af[filter->num_af] = afToRtnl(af);
	filter->num_af++;
	return 0;
};
