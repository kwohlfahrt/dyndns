#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <net/if.h>
#include <signal.h>

#include "util.h"
#include "filter.h"
#include "ipaddr.h"
#include "monitor.h"
#include "web_updater.h"

#ifdef WITH_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

static char const version[] = "0.0.2";

#define EXIT_USAGE EXIT_FAILURE + 1
static void printUsage(){
	puts("dyndns -V\n"
	     "dyndns -h\n"
	     "dyndns [-v] [-46] [--allow-temporary | -t] [--allow-private | -p] <interface> [URL]");
}

// Wrap for correct number of arguments
int stdoutUpdate(struct IPAddr addr, void* data) {
	return printAddr(addr);
}

int main(int const argc, char** argv) {
	struct AddrFilter filter = {.allow_private = false};
	void * updater;
	int (* addr_cb)(struct IPAddr, void*);

	// Deal with options

	const char short_opts[] = "vVh46pa";
	struct option long_opts[] = {
		{"allow-private", no_argument, 0, 'p'},
		{"allow-temporary", no_argument, 0, 't'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
	};
	bool verbosity = 0;
	int opt_index = 0;
	int opt;

	while ((opt = getopt_long(argc, argv, short_opts, long_opts, &opt_index)) != -1) {
		switch (opt) {
		case 'v':
			verbosity = 1;
			break;
		case 'V':
			puts(version);
			return EXIT_SUCCESS;
		case '4':
			filter.ipv4 = true;
			break;
		case '6':
			filter.ipv6 = true;
			break;
		case 'p':
			filter.allow_private = true;
			break;
		case 't':
			filter.allow_temporary = true;
			break;
		case 'h':
			printUsage();
			return EXIT_SUCCESS;
		default:
			printUsage();
			return EXIT_USAGE;
		}
	}

	// Listen for all changes if none specified.
	if (!(filter.ipv6 || filter.ipv4)) {
		filter.ipv6 = true;
		filter.ipv4 = true;
	}

	// Prepare updater, cleanup necessary if exiting after this point.
	switch ((argc - optind)){
	case 1:
		updater = NULL;
		addr_cb = stdoutUpdate;
		break;
	case 2:
		updater =  createUpdater();
		if (updater == NULL) {
			perror("Couldn't set up updating");
			return EXIT_FAILURE;
		}
		addr_cb = webUpdate;
		break;
	default:
		puts("Usage:\n");
		printUsage();
		return EXIT_USAGE;
	}

	char const * const iface_name = argv[optind];
	filter.iface = if_nametoindex(iface_name);
	if (!filter.iface) {
		fprintf(stderr, "Error resolving interface %s: %s\n",
			iface_name, strerror(errno));
		goto cleanup_updater;
	}

	if (verbosity){
		puts("Running in verbose mode.");
		printf("Listening on interfaces: %s (#%d)\n", iface_name, filter.iface);
		fputs("Listening for address changes in:", stdout);
		if (filter.ipv4) printf(" IPv4");
		if (filter.ipv6) printf(" IPv6");
		puts("");
	}

	int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (epoll_fd < 0) {
		perror("Couldn't create epoll");
		goto cleanup_updater;
	}

	struct Monitor * monitor = createMonitor(&filter, 1024, epoll_fd);
	if (monitor == NULL) {
		perror("Couldn't set up monitoring");
		goto cleanup_epoll;
	}

#ifdef WITH_SYSTEMD
	sd_notify(0, "READY=1");
#endif

	// Main loop
	do {
		struct epoll_event events[1];
		int nevents = epoll_wait(epoll_fd, events, NELEMS(events), -1);
		if (nevents < 0) {
			perror("Error waiting for events");
			goto cleanup;
		}

		for (int i = 0; i < nevents; i++) {
			events[i].data.ptr;
		}
	} while (true);

cleanup:
	destroyMonitor(monitor);
cleanup_epoll:
	close(epoll_fd);
cleanup_updater:
	destroyUpdater(updater);
	return EXIT_FAILURE;
}
