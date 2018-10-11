#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <resolv.h>
extern struct __res_state _res;

#include <string.h>
#ifndef HAVE_strlcpy
#include "strlcpy.h"
#define HAVE_strlcpy
#endif

#include <curl/curl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/inotify.h>
#include <sys/select.h>

#include "web_updater.h"
#include "ipaddr.h"
#include "monitor.h"
#include "filter.h"

static char const * url_template = "";
static size_t url_size = 0;
static char const * const ip_tag = "<ipaddr>";

size_t countIpTags(const char * template){
	size_t retval = 0;
	while ((template = strstr(template, ip_tag)) != NULL){
		retval++;
		template++;
	}
	return retval;
}

// No need to copy in current use. Might change.
void setUrl(char const * const new_url){
	url_template = new_url;
	url_size = strlen(url_template) + 1
	           + ( countIpTags(url_template)
	             * (INET6_ADDRSTRLEN - strlen(ip_tag)));
	return;
}

static char * templateUrl(struct IPAddr address, char * dst, size_t max_size){
	char ip_str[INET6_ADDRSTRLEN];
	if (!inet_ntop(address.af, &address, ip_str, sizeof(ip_str))){
		return NULL;
	}
	size_t ip_len = strlen(ip_str);
	size_t tag_len = strlen(ip_tag);

	size_t copy_len;
	char * write_pos = dst;
	const char * read_pos = url_template;
	const char * template_pos;

	while ((template_pos = strstr(read_pos, ip_tag)) != NULL){
		copy_len = template_pos - read_pos;
		if ((copy_len + ip_len) > max_size){
			errno = ENOSPC;
			return NULL;
		}
		memcpy(write_pos, read_pos, copy_len);
		write_pos += copy_len;
		memcpy(write_pos, ip_str, ip_len);
		write_pos += ip_len;

		max_size -= copy_len + ip_len;
		read_pos = template_pos + tag_len;
	}
	if (strlcpy(write_pos, read_pos, max_size) >= max_size){
			errno = ENOSPC;
			return NULL;
	}

	return dst;
}

static size_t discard(__attribute__((unused)) char *ptr,
                      size_t size, size_t nmemb,
		      __attribute__((unused)) void *userdata){
	return size * nmemb;
}

int getNameServers(struct IPAddr * const dst, size_t const num_addrs){
	res_init();
	for (int i = 0; i < _res.nscount && (size_t) i < num_addrs; ++i){
		if (_res.nsaddr_list[i].sin_addr.s_addr != 0){
			dst[i].af = AF_INET;
			dst[i].ipv4 = _res.nsaddr_list[i].sin_addr;
		} else if (_res._u._ext.nsaddrs[i] != NULL){
			dst[i].af = AF_INET6;
			dst[i].ipv6 = _res._u._ext.nsaddrs[i]->sin6_addr;
		} else {
			dst[i].af = AF_UNSPEC;
		}
	}

	return _res.nscount;
}

static bool waitForDNS(void){
	bool retval = false;

	#if MAXNS > 3
	#warn "MAXNS larger than expected value of 3"
	#endif
	struct IPAddr dns_servers[MAXNS]; // MAXNS == 3 so will be small
	int num_dns_servers;

	int fd = inotify_init();
	if (fd == -1){
		return false;
	}

	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	if (inotify_add_watch(fd, "/etc/resolv.conf", IN_MODIFY) == -1){
		goto cleanup;
	}

	res_init();
	num_dns_servers = getNameServers(dns_servers, _res.nscount);
	for (int i = 0; i < num_dns_servers; ++i){
		errno = 0;
		bool is_loopback = addrIsLoopback(dns_servers[i]);
		if (!is_loopback && errno != EINVAL){
			// we already have a DNS server
			retval = true;
			goto cleanup;
		}
	}

	// Only waiting on one file and one event, so contents don't matter
	// FIXME: Can lock up here if localhost is the only DNS server. Could add a timeout.
	ssize_t status = select(1, &fds, NULL, NULL, NULL);
	if (status != -1)
		retval = true;

cleanup:
	close(fd);
	return retval;
}

int webUpdate(struct IPAddr const addr){
	char * url;
	CURL * curl_handle = NULL;
	int retval = CURLE_OK;

	if ((url = malloc(url_size)) == NULL){
		retval = CURLE_OUT_OF_MEMORY;
		goto cleanup;
	}

	if (templateUrl(addr, url, url_size) == NULL){
		retval = CURLE_OUT_OF_MEMORY;
		goto cleanup;
	}

	if ((curl_handle = curl_easy_init()) == NULL){
		retval = CURLE_FAILED_INIT;
		goto cleanup;
	}

	if ((retval = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, discard)) != CURLE_OK)
		goto cleanup;

	if ((retval = curl_easy_setopt(curl_handle, CURLOPT_URL, url)) != CURLE_OK)
		goto cleanup;

	// If interface is recently brought up, this will fail with CURLE_COULDNT_RESOLVE_HOST
	// because NetworkManager hasn't set any DNS servers, so we will wait for one.
	retval = curl_easy_perform(curl_handle);
	if (retval == CURLE_COULDNT_RESOLVE_HOST){
		if (waitForDNS())
			retval = curl_easy_perform(curl_handle);
	}
	goto cleanup;

cleanup:
	if (retval == CURLE_OK)
		printf("Fetched URL: %s\n", url);
	else
		printf("Failed to fetch URL: %s (%s)\n", url, curl_easy_strerror(retval));

	free(url);
	curl_easy_cleanup(curl_handle);
	return retval;
}