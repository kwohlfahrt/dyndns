#ifndef _WEB_UPDATER_H
#define _WEB_UPDATER_H

#include <arpa/inet.h>
#include <curl/curl.h>
#include <signal.h>

#include "ipaddr.h"
#include "dyndns.h"

void setUrl(const char* new_url);
int webUpdate(struct IPAddr);

#endif /*_WEB_UPDATER_H*/
