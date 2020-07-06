#include <stdlib.h>

#include "updater.h"

Updater_t createPrintUpdater() {
	Updater_t updater = malloc(sizeof(*updater));
	updater->tag = EPOLL_UPDATER;
	updater->data.tag = PRINT_UPDATER;
	return updater;
}

int update(Updater_t updater, struct IPAddr addr) {
	switch (updater->data.tag) {
	case PRINT_UPDATER:
		return printAddr(addr);
	case WEB_UPDATER:
		return webUpdate(&updater->data.data.web, addr);
	};
	// Should be unreachable
	return -1;
}

void destroyUpdater(Updater_t updater) {
	switch (updater->data.tag) {
	case PRINT_UPDATER:
		break;
	case WEB_UPDATER:
		destroyWebUpdater(&updater->data.data.web);
		break;
	};
	free(updater);
}
