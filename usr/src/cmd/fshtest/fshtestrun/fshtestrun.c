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

/*
 * TODO: Change lt to Kele-defined list.
 */


extern int errno;

#define	MAX_FILES 100
static char paths[MAX_FILES][MAXPATHLEN];
static int paths_count;
static char *mntpath;

#define MAX_ARGS 1000000
#define	MAX_ITERATIONS MAX_ARGS

static int drv_fd;
static hook_list_t free_hooks;
static hook_list_t installed_hooks;

void
install_random_hook()
{
	int pos, val;
	hook_t *elem;

	if (lcount(&free_hooks) == 0)
		return;
	
	pos = rand() % lcount(&free_hooks);
	elem = lhead(&free_hooks);
	while (pos--)
		elem = lnext(elem);

	val = elem->val;
	lremove(&free_hooks, elem);
	linsert_head(&installed_hooks, val);

	if (fsht_install_hook(drv_fd, mntpath, val) == -1) 
		perror("fsh_install_hook: ");
}

void
remove_random_hook()
{
	int pos, val;
	hook_t *elem;

	if (lcount(&installed_hooks) == 0)
		return;
	
	pos = rand() % lcount(&installed_hooks);
	elem = lhead(&installed_hooks);
	while (pos--)
		elem = lnext(elem);

	val = elem->val;
	lremove(&installed_hooks, elem);
	linsert_head(&free_hooks, val);

	if (fsht_remove_hook(drv_fd, mntpath, val) == -1)
		perror("fsh_remove_hook: ");
}

void
print_hooks(const char *func)
{
	hook_t *elem = lhead(&installed_hooks);
	while (elem != NULL) {
		(void) printf("fsht_hook_pre_%s %d\n", func, elem->val);
		elem = lnext(elem);
	}
}

void
run_test(int iterations)
{
	int fd, i, op;

	linit(&free_hooks);
	linit(&installed_hooks);
	for (i = 0; i < MAX_ARGS; i++)
		linsert_head(&free_hooks, i);


	for (i = 0; i < iterations; i++) {
		usleep(100);
		switch (op = rand() % 4) {
		case 0:
			install_random_hook();	
			break;

		case 1:
			remove_random_hook();
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
}

int
main(int argc, char *argv[])
{
	FILE *files_fd;
	int index;
	int iterations, tests;
	hook_t *elem;

	if (argc != 5) {
		(void) printf(
		    "Usage: fshtestrun tests iterations mntpoint pathfile\n"
		    "\ttests - number of tests to run\n"
		    "\titerations - number of iterations to run in each test\n"
		    "\tmntpoint - mountpoint where the files are present\n"
		    "\tpathfile - file containing paths to files on which\n"
		    "\t           read/write operations should be performed.\n"
		    "\t           These files should be in the filesystem\n"
		    "\t           given in the mntpoint\n\n");
		return (-1);
	}


	/* Read args */
	tests = atoi(argv[1]);
	iterations = atoi(argv[2]);
	if (iterations > MAX_ITERATIONS) {
		(void) fprintf(stderr, 
		    "Maximum number of iterations is set to 10000\n");
		return (-1);
	}
	mntpath = argv[3];

	files_fd = fopen(argv[4], "r");
	if (files_fd == NULL) {
		perror("Error");
		return (-1);
	}
	index = 0;
	while (fgets(paths[index], MAXPATHLEN, files_fd) != NULL) {
		paths[index][strlen(paths[index]) - 1] = '\0';
		index++;	
		if (index >= MAX_FILES) {
			(void) fprintf(stderr, "Error: Too many files. "
			   "The limit is set to less than %d.\n",
			   MAX_FILES);
			(void) fclose(files_fd);
			return (-1);
		}
	}
	paths_count = index;
	(void) fclose(files_fd);
	


	/* Run tests */
	srand(time(0));
	if ((drv_fd = fsht_open()) == -1) {
		perror("Error");
		return (-1);
	}
	(void) fsht_enable(drv_fd);
	run_test(iterations);
	(void) fsht_disable(drv_fd);

	/* Cleaning up */
	while ((elem = lhead(&installed_hooks)) != NULL) {
		(void) fsht_remove_hook(drv_fd, mntpath, elem->val);
		lremove_head(&installed_hooks);
	}
	lclear(&free_hooks);

	fsht_close(drv_fd);

	return (0);
}
