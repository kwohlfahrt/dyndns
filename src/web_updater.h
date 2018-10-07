#ifndef _WEB_UPDATER_H
#define _WEB_UPDATER_H

#include "ipaddr.h"

void setUrl(char const * const new_url);
int webUpdate(struct IPAddr const addr);

#endif /*_WEB_UPDATER_H*/
