#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include <curl/curl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <net/if.h>

#include <sys/mman.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include "filter.h"
#include "dyndns.h"
#include "monitor.h"
#include "web_updater.h"

static const char version[] = "0.0.1";
unsigned int verbosity = 0;

void printUsage(){
	puts("dyndns -V\n"
	     "dyndns -h\n"
	     "dyndns [-v] [-p] [-4] [-6] interface [url]");
}

int main(int argc, char** argv) {
	struct filter filter = {};
	int (* addr_processor)(struct IPAddr);

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
	
	struct SharedAddr * shared_data;
	// mmap'ed memory automatically unmaps at program exit
	if ((shared_data = mmap(NULL, sizeof(&shared_data), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
		perror("Could not create shared memory");
		return EXIT_FAILURE;
	}

	pthread_mutexattr_t mattr;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init(&shared_data->mutex, &mattr);
	pthread_mutexattr_destroy(&mattr);
	// TODO: Deal with process being killed while mutex is locked.
	// Maybe semaphores? Block signals?
	
	sigset_t parent_sigmask;
	sigemptyset(&parent_sigmask);
	sigaddset(&parent_sigmask, SIGUSR1);
	sigaddset(&parent_sigmask, SIGCHLD);

	sigprocmask(SIG_SETMASK, &parent_sigmask, NULL);

	pid_t monitor_child_pid = 0, process_child_pid = 0;
	// TODO: Bring monitoring back into main process? Make monitorAddrs a blocking call returning a struct IPAddr
	if ((monitor_child_pid = fork()) == -1){
		perror("Could not fork IP monitoring process.");
		goto cleanup;
	} else if (!monitor_child_pid) {
		sigset_t monitor_sigmask;
		sigemptyset(&monitor_sigmask);

		sigprocmask(SIG_SETMASK, &monitor_sigmask, NULL);
		return monitorAddrs(filter, shared_data);
	}

	while (true) {
		siginfo_t info;
		int signal = sigwaitinfo(&parent_sigmask, &info);
		switch (signal){
		case SIGUSR1:
			if ((process_child_pid = fork()) == -1){
				perror("Could not fork IP processing process.");
				goto cleanup;
			} else if (!process_child_pid) {
				// Probably better via exec now that only one address is processed at a time.
				sigset_t process_sigmask;
				sigemptyset(&process_sigmask);
				sigprocmask(SIG_SETMASK, &process_sigmask, NULL);
				
				struct IPAddr addr;
				pthread_mutex_lock(&shared_data->mutex);
				addr = shared_data->addr;
				pthread_mutex_unlock(&shared_data->mutex);
				return addr_processor(addr);
			}
			break;
		case SIGCHLD:{
			pid_t child_pid;
			while ((child_pid = waitpid(-1, NULL, WNOHANG))){
				if (child_pid < 0){
					perror("Could not get status of monitor process");
					goto cleanup;
				}
				else if (child_pid == monitor_child_pid){
					// Monitor dead.
					goto cleanup;
				}
			}
			break;
		}
		default:
			// should never trigger
			goto cleanup;
		}
	}

cleanup:
	puts("Cleaning up...");
	signal(SIGTERM, SIG_IGN);
	kill(-getpid(), SIGTERM); // SIGCHLD and SIGUSR1 are still blocked 
	                          // POSIX guarantees at least one signal delivered before kill() returns
	signal(SIGTERM, SIG_DFL);
	sigprocmask(SIG_UNBLOCK, &parent_sigmask, NULL);

	while (true){
		int status;
		pid_t child_pid = waitpid(-1, &status, 0);

		char monitor_label[] = "Monitor", processing_label[] = "Processing";
		char * child_label = (child_pid == monitor_child_pid) ? monitor_label : processing_label;

		if (child_pid == -1) {
			if (errno == ECHILD)
				break;
			else
				perror("Error waiting for children.");
		}

		if (WIFEXITED(status))
			printf("%s child exited with status %d\n", child_label, WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			printf("%s child terminated by signal %d\n", child_label, WTERMSIG(status));
		else
			printf("%s child exited abnormally.", child_label);
	}

	pthread_mutex_destroy(&shared_data->mutex);
	return 0;
}
