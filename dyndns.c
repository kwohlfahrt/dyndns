#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <net/if.h>
#include <sys/wait.h>
#include <signal.h>

#include "filter.h"
#include "ipaddr.h"
#include "monitor.h"
#include "web_updater.h"

static char const version[] = "0.0.1";
unsigned int verbosity = 0;
static int const termsig = SIGQUIT;

static void printUsage(){
	puts("dyndns -V\n"
	     "dyndns -h\n"
	     "dyndns [-v] [-p] [-4] [-6] interface [url]");
}

static bool childOK(int const status){
	if (WIFEXITED(status)){
		if (WEXITSTATUS(status) != 0) {
			fprintf(stderr, "Processing child exited with status %d\n", WEXITSTATUS(status));
			return false;
		}
	} else if (WIFSIGNALED(status)){
		if (WTERMSIG(status) != termsig){
			fprintf(stderr, "Processing child terminated by signal %d\n", WTERMSIG(status));
			return false;
		}
	} else {
		fputs("Processing child exited abnormally.", stderr);
		return false;
	}
	return true;
}

int main(int argc, char** argv) {
	struct AddrFilter filter = {};
	int (* addr_processor)(struct IPAddr);

	// Deal with options

	const char short_opts[] = "vVh46";
	struct option long_opts[] = {
		{"allow-private", no_argument, 0, 'p'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
	};
	int opt_index = 0;
	char opt;
	
	while ((opt = getopt_long(argc, argv, short_opts, long_opts, &opt_index)) != -1) {
		switch (opt) {
		case 'v':
			if (verbosity < 3)
				++verbosity;
			break;
		case 'V':
			puts(version);
			return EXIT_SUCCESS;
		case '4':
			addFilterAf(&filter, AF_INET);
			break;
		case '6':
			addFilterAf(&filter, AF_INET6);
			break;
		case 'p':
			filter.allow_private = true;
			break;
		case 'h':
			printUsage();
			return EXIT_SUCCESS;
		default:
			printUsage();
			return 2;
		}
	}
	
	// Listen for all changes if none specified.
	if (filter.num_af == 0){
		addFilterAf(&filter, AF_INET);
		addFilterAf(&filter, AF_INET6);
	}

	switch ((argc - optind)){
	case 1:
		addr_processor = printAddr;
		break;
	case 2:
		setUrl(argv[optind + 1]);
		addr_processor = webUpdate;
		break;
	default:
		puts("Usage:\n");
		printUsage();
		return 2;
	}
	
	char * iface_name = argv[optind];
	filter.iface = if_nametoindex(iface_name);
	if (!filter.iface) {
		fprintf(stderr, "Error resolving interface %s: %s\n",
			iface_name, strerror(errno));
		return EXIT_FAILURE;
	}
	
	if (verbosity)
		printf("Running with verbosity: %i\n", verbosity);
	if (verbosity > 0) {
		printf("Listening on interfaces: %s\n", iface_name);
		fputs("Listening for address changes in:", stdout);
		if (checkFilterAf(filter, AF_INET)) 
			printf(" IPv4");
		if (checkFilterAf(filter, AF_INET6)) 
			printf(" IPv6");
		puts("");
	}

	// Prepare monitoring

	ssize_t sock = createAddrSocket(filter);
	if (sock == -1){
		perror("Couldn't create monitoring socket");
		goto cleanup;
	}

	if (!requestAddr(filter, sock)){
		perror("Couldn't request current address");
		goto cleanup;
	}

	// Main loop

	pid_t child = -1;
	do {
		struct IPAddr new_addr = nextAddr(filter, sock);
		if (child != -1){
			// Make sure kill isn't called on first loop.
			kill(child, termsig);
			
			int status;
			if (waitpid(child, &status, 0) == -1){
				perror("Error waiting for child");
				break;
			}
			if (!childOK(status))
				break;
		}

		if (new_addr.af == AF_MAX){
			perror("An error occurred while waiting for a new IP");
			break;
		} else if (new_addr.af == AF_UNSPEC) {
			fputs("Netlink socket closed by kernel.", stderr);
			break;
		}

		// TODO: Could use exec
		child = fork();
		if (child == -1){
			perror("Could not fork to process new address.");
			break;
		} else if (!child){
			close(sock); // Make sure to set CLOEXEC if changing to exec.
			return addr_processor(new_addr);
		}
	} while (true);

cleanup:
	close(sock);
	return 0;
}
