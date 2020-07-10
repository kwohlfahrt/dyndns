#include <stdlib.h>

#include "updater.h"

Updater_t createPrintUpdater() {
	Updater_t updater = malloc(sizeof(*updater));
	updater->tag = PRINT_UPDATER;
	return updater;
}

int update(Updater_t updater, struct IPAddr addr) {
	switch (updater->tag) {
	case PRINT_UPDATER:
		return printAddr(addr);
	case WEB_UPDATER:
		return webUpdate(&updater->web, addr);
	};
	// Should be unreachable
	return -2;
}

int handleMessage(Updater_t updater, int fd, int32_t events) {
	switch (updater->tag) {
	case PRINT_UPDATER:
		return -2;
	case WEB_UPDATER:
		return handleWebMessage(&updater->web, fd, events);
	}
	return -2;
};

int handleTimeout(Updater_t updater) {
	switch (updater->tag) {
	case PRINT_UPDATER:
		// Unreachable
		return -2;
	case WEB_UPDATER:
		return handleWebTimeout(&updater->web);
	}
	return -2;
}

void destroyUpdater(Updater_t updater) {
	switch (updater->tag) {
	case PRINT_UPDATER:
		break;
	case WEB_UPDATER:
		destroyWebUpdater(&updater->web);
		break;
	};
	free(updater);
}
