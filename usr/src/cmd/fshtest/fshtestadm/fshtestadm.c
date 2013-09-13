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
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../common/common.h"

extern int errno;

static int drv_fd;

#define	MAXFILES 100
static char paths[MAXFILES][MAXPATHLEN];
static int paths_count;

#define	MAXHOOKS 100000
typedef struct dummy_hook {
	int64_t	handle;
	int	arg;
} dummy_hook_t;

static dummy_hook_t installed_hooks[MAXHOOKS];
static int installed_hooks_count;

static int arg;

void
rand_dummy_install()
{
	int64_t handle;

	if (!(installed_hooks_count + 1 < MAXHOOKS))
		return;

	handle = fsht_hook_install(drv_fd, paths[0], FSHTT_DUMMY, arg++);
	if (handle == -1) {
		(void) fprintf(stderr, "Hook limit exceeded.\n");

	} else if (handle == -2) {
		(void) fprintf(stderr, "open() failed: %s\n", strerror(errno));

	} else if (handle == -3) {
		(void) fprintf(stderr,
		    "Bad type passed to fsht_hook_install()\n");
	}

	installed_hooks[installed_hooks_count].arg = arg++;
	installed_hooks[installed_hooks_count].handle = handle;
	installed_hooks_count++;
}

void
rand_dummy_remove()
{
	int pos;
	pos = rand() % installed_hooks_count;

	if (fsht_hook_remove(drv_fd, installed_hooks[pos].handle) != 0)
		(void) fprintf(stderr, "fsht_hook_remove() failed\n");

	installed_hooks[pos].handle =
	    installed_hooks[installed_hooks_count - 1].handle;
	installed_hooks[pos].arg =
	    installed_hooks[installed_hooks_count - 1].arg;

	installed_hooks_count--;
}

void
print_hooks(const char *func)
{
	int i;
	for (i = installed_hooks_count - 1; i >= 0; i--)
		(void) printf("pre %s %d\n", func, installed_hooks[i].arg);

	for (i = 0; i < installed_hooks_count; i++)
		(void) printf("post %s %d\n", func, installed_hooks[i].arg);
}

void
diagnose()
{
	int i;
	int64_t handles[4];
	int fd;
	char buf[100];

	handles[0] = fsht_hook_install(drv_fd, paths[0], FSHTT_PREPOST, 0);
	handles[1] = fsht_hook_install(drv_fd, paths[0], FSHTT_API, 0);
	handles[2] = fsht_hook_install(drv_fd, paths[0], FSHTT_AFTER_REMOVE, 0);
	handles[3] = fsht_hook_install(drv_fd, paths[0], FSHTT_SELF_DESTROY, 0);

	for (i = 0; i < 4; i++) {
		if (handles[i] < 0) {
			(void) fprintf(stderr, "Diagnose failed for: "); 
			switch (i) {
			case 0:
				(void) fprintf(stderr, "pre-post\n");
				break;
			case 1:
				(void) fprintf(stderr, "api\n");
				break;
			case 2:
				(void) fprintf(stderr, "after remove\n");
				break;
			case 3:
				(void) fprintf(stderr, "self destroy\n");
				break;
			}
		}

		if (handles[i] == -1)
			(void) fprintf(stderr, "Hook limit exceeded.\n");
		else if (handles[i] == -2)
			(void) fprintf(stderr, "open() failed! %s\n",
			    strerror(errno));
		else if (handles[i] == -3)
			(void) fprintf(stderr, 
			    "Bad type passed to fsht_hook_install()\n");
	}

	fd = open(paths[0], O_RDONLY);
	if (fd == -1)
		(void) fprintf(stderr, "open() failed! %s\n", strerror(errno));

	(void) read(fd, buf, 100);
	
	/*
	 * - handles[3] is already removed
	 */
	for (i = 0; i < 3; i++) {
		if (handles[i] > 0)
			if (fsht_hook_remove(drv_fd, handles[i]) != 0)
				(void) fprintf(stderr,
				    "fsht_hook_remove failed!");
	}

	/*
	 * read() again for handles[2] test
	 */
	(void) read(fd, buf, 100);

	if (fd != -1)
		(void) close(fd);
}

void
run_test(int iterations)
{
	int fd, i, op;

	arg = 0;

	diagnose();

	for (i = 0; i < iterations; i++) {
		(void) usleep(1000);
		switch (op = rand() % 4) {
		case 0:
			rand_dummy_install();
			break;

		case 1:
			if (installed_hooks_count > 0)
				rand_dummy_remove();
			break;

		case 2:
		case 3: {
			/* read, write */

			char buf[100];
			int choice = rand() % paths_count;

			fd = open(paths[choice], O_RDWR);
			if (fd == -1) {
				(void) fprintf(stderr,
				    "Error: Cannot open file %s: %s\n",
				    paths[choice], strerror(errno));
				return;
			}

			if (op == 2) {
				(void) read(fd, buf, 100);
				print_hooks("read");

			} else { /* op == 3 */
				(void) write(fd, buf, 100);
				print_hooks("write");
			}

			(void) close(fd);
			break;
		}

		default:
			break;
		}
	}

	for (i = 0; i < installed_hooks_count; i++)
		(void) fsht_hook_remove(drv_fd, installed_hooks[i].handle);
	installed_hooks_count = 0;
}

int
main(int argc, char *argv[])
{
	FILE *files_fd;
	int index, i;
	int iterations, tests;

	srand(time(0));

	if (argc != 4) {
		(void) printf(
		    "Usage: fshtestrun tests iterations mntpoint pathfile\n"
		    "\ttests - number of tests to run\n"
		    "\titerations - number of iterations to run in each test\n"
		    "\tpathfile - textfile containing paths to files on which\n"
		    "\t           read/write operations should be performed.\n"
		    "\t           Hook will be installed on the filesystems\n"
		    "\t           containing these files. All the files\n"
		    "\t		  should be in the same filesystem!"
		    "\n\n");
		return (-1);
	}
	
	/* Read args */
	tests = atoi(argv[1]);
	iterations = atoi(argv[2]);
	if (iterations > MAXHOOKS) {
		(void) fprintf(stderr,
		    "Maximum number of iterations is set to %d\n", MAXHOOKS);
		return (-1);
	}

	files_fd = fopen(argv[3], "r");
	if (files_fd == NULL) {
		perror("Error");
		return (-1);
	}
	index = 0;
	while (fgets(paths[index], MAXPATHLEN, files_fd) != NULL) {
		paths[index][strlen(paths[index]) - 1] = '\0';
		index++;
		if (index >= MAXFILES) {
			(void) fprintf(stderr, "Error: Too many files. "
			    "The limit is set to less than %d.\n",
			    MAXFILES);
			(void) fclose(files_fd);
			return (-1);
		}
	}
	paths_count = index;
	(void) fclose(files_fd);

	/* Run tests */
	for (i = 0; i < tests; i++) {
		if ((drv_fd = fsht_open()) == -1) {
			perror("Error");
			return (-1);
		}
		(void) fsht_enable(drv_fd);
		run_test(iterations);
		(void) fsht_disable(drv_fd);
	}
	fsht_close(drv_fd);

	return (0);
}
