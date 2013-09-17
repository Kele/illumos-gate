/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2013 Damian Bogel.  All rights reserved.
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../common/common.h"

/*
 * Stress test for fsh. The only output provided by this program can be:
 * - error on opening the driver
 * - errors on initial/final enable/disable
 *
 * Usage: stress mountpoint number_of_tests
 */

#define MAX_HANDLES 1000000
int64_t handles[MAX_HANDLES];
int n;

int main(int argc, char *argv[])
{
	int drv_fd;
	int tests;
	char *mnt;

	if (argc != 3) {
		(void) fprintf(stderr,
		    "Usage: stress mountpoint number_of_tests\n");
		return (-1);
	}

	tests = atoi(argv[2]);
	mnt = argv[1];

	drv_fd = fsht_open();
	if (drv_fd == -1) {
		(void) fprintf(stderr, "Cannot open mountpoint. %s\n",
		    strerror(errno));
		return (1);
	}

	srand(time(0));
	fsht_enable(drv_fd);
	while (tests--) {
		switch (rand() % 9) {
		case 0:
			fsht_enable(drv_fd);
			break;

		case 1:
			fsht_disable(drv_fd);
			break;

		case 2:	/* FALLTHROUGH */
		case 3:
		case 4:
			handles[n] = fsht_hook_install(drv_fd, mnt,
			    FSHTT_DUMMY, rand() % 100);
			n++;
			break;

		case 5:	/* FALLTHROUGH */
		case 6:
		case 7: {
			if (n == 0)
				break;

			int pos = rand() % n;

			fsht_hook_remove(drv_fd, handles[pos]);
			handles[pos] = handles[n - 1];
			n--;
			break;
		}
		case 8:
			fsht_hook_remove(drv_fd, rand());
			break;
		}
	}
	fsht_disable(drv_fd);
	
	fsht_close(drv_fd);

	return (0);
}
