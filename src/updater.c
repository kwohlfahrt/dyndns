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
	return -2;
}

int handleMessage(Updater_t updater, struct epoll_event * ev) {
	switch (updater->data.tag) {
	case PRINT_UPDATER:
		return -2;
	case WEB_UPDATER:
		return handleWebMessage(&updater->data.data.web, updater->fd, ev);
	}
	return -2;
};

int handleTimeout(Updater_t updater) {
	switch (updater->data.tag) {
	case PRINT_UPDATER:
		// Unreachable
		return -2;
	case WEB_UPDATER:
		return handleWebTimeout(&updater->data.data.web);
	}
	return -2;
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
