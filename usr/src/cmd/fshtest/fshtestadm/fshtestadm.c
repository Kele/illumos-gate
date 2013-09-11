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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stropts.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../common/common.h"

int mflag;
int aflag;
int iflag;
int rflag;
int hflag;
int cflag;
int eflag;
int dflag;

static int drv_fd;

static int arg;
static char *mnt;

void
read_args(int argc, char *argv[])
{
	extern char *optarg;
	int opt;

	while ((opt = getopt(argc, argv, "edm:a:irhc")) != -1) {
		switch (opt) {
		case 'e':
			eflag = 1;
			break;

		case 'd':
			dflag = 1;
			break;

		case 'm':
			mflag = 1;
			mnt = optarg;
			break;

		case 'a':
			aflag = 1;
			arg = atoi(optarg);
			break;

		case 'i':
			iflag = 1;
			break;

		case 'r':
			rflag = 1;
			break;

		case 'h':
			hflag = 1;
			break;

		case 'c':
			cflag = 1;
			break;
		}
	}
}

int
main(int argc, char *argv[])
{
	int err;

	if (argc < 2)
		goto usage;

	read_args(argc, argv);

	if ((drv_fd = fsht_open()) == -1) {
		perror("Error");
		return (-1);
	}

	if (eflag) {
		err = fsht_enable(drv_fd);
		if (err)
			(void) fprintf(stderr, "Error: %s\n", strerror(err));

	} else if (dflag) {
		err = fsht_disable(drv_fd);
		if (err)
			(void) fprintf(stderr, "Error: %s\n", strerror(err));

	} else if (hflag) {
		int fd;

		if (!(mflag && aflag) || (iflag && rflag) || !(iflag || rflag))
			goto usage;

		fd = open(mnt, O_RDONLY);
		if (fd == -1) {
			perror("Error");

		} else {

			if (iflag)
				err = fsht_install_hook(drv_fd, mnt, arg);
			else
				err = fsht_remove_hook(drv_fd, mnt, arg);

			if (err == -1)
				perror("Error");
			else if (err != 0)
				(void) fprintf(stderr, "Error: %s\n",
				    strerror(err));
		}
		(void) close(fd);

	} else if (cflag) {
		(void) fprintf(stderr, "Error: not supported yet\n");
	} else {
		goto usage;
	}

	fsht_close(drv_fd);

	return (0);

usage:
	fsht_close(drv_fd);

	(void) printf(
	    "Usage: fshtestadm [-ed] [-m mntpath] -a arg [-ir] [-hc]\n"
	    "\t-e - enable fshtest\n"
	    "\t-d - disable fshtest\n"
	    "\t-m - mntpath for hook installing or removing\n"
	    "\t-a - arg is a number which is passed to the hook\n"
	    "\t-i - install\n"
	    "\t-r - remove\n"
	    "\t-h - hook\n"
	    "\t-c - callback (global)\n"
	    "\n");

	return (0);
}
