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
typedef struct hook {
	int64_t	handle;
	int	arg;
	int	type;
} hook_t;

static hook_t installed_hooks[MAXHOOKS];
static int installed_hooks_count;

static int arg;

const char *
xlate_type(int type)
{
	switch(type) {
	case FSHTT_DUMMY:
		return ("FSHTT_DUMMY");
	case FSHTT_PREPOST:
		return ("FSHTT_PREPOST");
	case FSHTT_API:
		return ("FSHTT_API");
	case FSHTT_AFTER_REMOVE:
		return ("FSHTT_AFTER_REMOVE");
	case FSHTT_SELF_DESTROY:
		return ("FSHTT_SELF_DESTROY");
	default:
		return ("<bad type>");
	}
}

int64_t
hook_install(char *path, int type)
{
	int64_t handle;
	int real_arg;

	if (installed_hooks_count + 1 >= MAXHOOKS)
		return (-1);
	
	if (type == FSHTT_DUMMY) {
		real_arg = arg++;
	} else if (type == FSHTT_API) {
		real_arg = 0;
	} else {
		real_arg = 0xB06E1;
	}

	handle = fsht_hook_install(drv_fd, path, type, real_arg);

	arg++;
	if (handle == -1) {
		(void) fprintf(stderr, "Hook limit exceeded.\n");

	} else if (handle == -2) {
		(void) fprintf(stderr, "open() failed: %s\n", strerror(errno));

	} else if (handle == -3) {
		(void) fprintf(stderr,
		    "Bad type passed to fsh_hook_install()\n");
	}

	(void) printf("fsh_hook_install(op = %d, magic1 = %d) "
	    "= %lld\n", type, real_arg, handle);

	installed_hooks[installed_hooks_count].arg = real_arg;
	installed_hooks[installed_hooks_count].handle = handle;
	installed_hooks[installed_hooks_count].type = type;
	installed_hooks_count++;

	return (handle);
}

void
hook_remove(int64_t handle, int pos, int cb)
{
	int i;

	if (pos != -1) {
		handle = installed_hooks[pos].handle;

	} else {
		int i;
		for (i = 0; i < installed_hooks_count; i++) {
			if (installed_hooks[i].handle == handle) {
				pos = i;
				break;
			}
		}
	}

	(void) printf("fsh_hook_remove(%lld)\n", handle);

	if (fsht_hook_remove(drv_fd, handle) != 0)
		(void) fprintf(stderr, "fsh_hook_remove() failed\n");
	else if (cb) {
		(void) printf("fsht_remove_cb(%lld)\n", handle);
		if (installed_hooks[pos].type == FSHTT_API) {
			(void) printf("fsh_hook_install(EMPTY)\n");
			(void) printf("fsh_hook_remove(EMPTY)\n");
		}
	}

	for (i = pos; i < installed_hooks_count - 1; i++) {
		(void) memcpy(&installed_hooks[i],
		    &installed_hooks[i + 1], sizeof (hook_t));
	}
	installed_hooks_count--;
}

void
rand_dummy_install()
{
	(void) hook_install(paths[0], FSHTT_DUMMY);
}

void
rand_dummy_remove()
{
	int pos;
	pos = rand() % installed_hooks_count;
	
	hook_remove(-1, pos, 1);	
}

void
print_hooks(const char *funcname)
{
	int i;
	for (i = installed_hooks_count - 1; i >= 0; i--) {
		(void) printf("pre %s:\thandle = %lld\n",
		    funcname, installed_hooks[i].handle);

		switch (installed_hooks[i].type) {
		case FSHTT_API:
			(void) printf("fsh_hook_install(EMPTY)\n");
			(void) printf("fsh_hook_remove(EMPTY)\n");
			break;
		
		case FSHTT_SELF_DESTROY:
			(void) printf("fsh_hook_remove(%lld)\n",
			    installed_hooks[i].handle);
			break;

		default:
			break;
		}
	}

	for (i = 0; i < installed_hooks_count; i++) {
		(void) printf("post %s:\thandle = %lld\n",
		    funcname, installed_hooks[i].handle);

		switch (installed_hooks[i].type) {
		case FSHTT_API:
			(void) printf("fsh_hook_install(EMPTY)\n");
			(void) printf("fsh_hook_remove(EMPTY)\n");
			break;
		
		case FSHTT_SELF_DESTROY:
			break;

		default:
			break;
		}
	}

	for (i = 0; i < installed_hooks_count; i++) {
		if (installed_hooks[i].type == FSHTT_SELF_DESTROY)
			(void) printf("fsht_remove_cb(%lld)\n",
			    installed_hooks[i].handle);
	}
}

void
run_read(const char *path)
{
	char buf[100];
	int fd;

	if ((fd = open(path, O_RDONLY)) == -1) {
		(void) fprintf(stderr, "open() failed for %s\n", path);
		perror("Error");
		return;
	}
	
	(void) read(fd, buf, 100);
	print_hooks("read");

	(void) close(fd);
}

void
diagnose()
{
	int i;
	int64_t handles[4];
	int types[4] = { FSHTT_PREPOST, FSHTT_API, FSHTT_AFTER_REMOVE,
	    FSHTT_SELF_DESTROY };
	
	for (i = 0; i < 4; i++) {
		handles[i] = hook_install(paths[0], types[i]);

		if (handles[i] < 0) {
			(void) fprintf(stderr, "Diagnose failed for: %s",
			    xlate_type(types[i]));
		}
	}
	
	run_read(paths[0]);
	
	/* FSHTT_SELF_DESTROY is already removed */
	for (i = 0; i < 4; i++) {
		if (handles[i] > 0)
			hook_remove(handles[i], -1, 1);
	}

	/* read() again for FSHTT_AFTER_REMOVE test */
	run_read(paths[0]);
}

void
run_test(int iterations)
{
	int i, op;

	arg = 0;

	diagnose();

	for (i = 0; i < iterations; i++) {
		(void) usleep(1000);	/* So that DTrace could follow. */
		switch (op = rand() % 3) {
		case 0:
			rand_dummy_install();
			break;

		case 1:
			if (installed_hooks_count > 0)
				rand_dummy_remove();
			break;

		case 2:
			run_read(paths[rand() % paths_count]);
			break;

		default:
			break;
		}
	}

	while (installed_hooks_count > 0)
		hook_remove(-1, 0, 1);
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
