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
static int const termsig = SIGQUIT;

#define EXIT_USAGE EXIT_FAILURE + 1
static void printUsage(){
	puts("dyndns -V\n"
	     "dyndns -h\n"
	     "dyndns [-v] [-46] [--allow private | -p] [--process-all | -a] <interface> [URL]");
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

int main(int const argc, char** argv) {
	struct AddrFilter filter = {.allow_private = false};
	int (* addr_processor)(struct IPAddr);

	// Deal with options

	const char short_opts[] = "vVh46pa";
	struct option long_opts[] = {
		{"allow-private", no_argument, 0, 'p'},
		{"process-all", no_argument, 0, 'a'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
	};
	bool verbosity = 0;
	bool process_all = false;
	int opt_index = 0;
	char opt;
	
	while ((opt = getopt_long(argc, argv, short_opts, long_opts, &opt_index)) != -1) {
		switch (opt) {
		case 'a':
			process_all = true;
			break;
		case 'v':
			verbosity = 1;
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
			return EXIT_USAGE;
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
		return EXIT_USAGE;
	}
	
	char const * const iface_name = argv[optind];
	filter.iface = if_nametoindex(iface_name);
	if (!filter.iface) {
		fprintf(stderr, "Error resolving interface %s: %s\n",
			iface_name, strerror(errno));
		return EXIT_FAILURE;
	}
	
	if (verbosity){
		puts("Running in verbose mode.");
		printf("Listening on interfaces: %s (#%d)\n", iface_name, filter.iface);
		fputs("Listening for address changes in:", stdout);
		if (checkFilterAf(filter, AF_INET)) 
			printf(" IPv4");
		if (checkFilterAf(filter, AF_INET6)) 
			printf(" IPv6");
		puts("");
	}

	// Prepare monitoring, cleanup necessary if exiting after this point.
	struct MonitorState state;
	if (!initState(filter, &state, 1024)){
		perror("Couldn't set up for monitoring");
		return EXIT_FAILURE;
	}

	// Main loop

	pid_t child = -1;
	do {
		struct IPAddr new_addr = nextAddr(filter, &state);
		if (child != -1){
			// Make sure kill isn't called on first loop.
			if (!process_all)
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
			close(state.socket); // Make sure to set CLOEXEC if changing to exec.
			return addr_processor(new_addr);
		}
	} while (true);

	close(state.socket);
	free(state.buf);
	return EXIT_FAILURE;
}
