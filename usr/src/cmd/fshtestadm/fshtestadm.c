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
#include <sys/fshtest.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int mflag;
int aflag;
int iflag;
int rflag;
int hflag;
int cflag;
int eflag;
int dflag;

int drv_fd;

int arg;
char *mnt;

int
main(int argc, char *argv[])
{
	extern char *optarg;
	int opt;


	if (argc < 2)
		goto usage;

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

	if (eflag) {
		drv_fd = open(FSHT_DEV_PATH, O_RDWR);
		if (drv_fd == -1) {
			perror("Error");
			return (-1);
		}
		(void) ioctl(drv_fd, FSHT_ENABLE);
		(void) close(drv_fd);
		return (0);
	}

	if (eflag) {
		drv_fd = open(FSHT_DEV_PATH, O_RDWR);
		if (drv_fd == -1) {
			perror("Error");
			return (-1);
		}
		(void) ioctl(drv_fd, FSHT_DISABLE);
		(void) close(drv_fd);
		return (0);
	}

	if (hflag) {
		fsht_hook_ioc_t io;
		int fd;

		if (!mflag || !aflag)
			goto usage;

		fd = open(mnt, O_RDONLY);
		if (fd == -1) {
			perror("Error");
			return (-1);
		}

		if (iflag) {
			int ret;

			io.fshthio_fd = fd;
			io.fshthio_arg = arg;

			drv_fd = open(FSHT_DEV_PATH, O_RDWR);
			if (drv_fd == -1) {
				perror("Error");
				(void) close(fd);
				return (-1);
			}

			ret = ioctl(drv_fd, FSHT_HOOKS_INSTALL, &io);
			if (ret != 0) {
				(void) fprintf(stderr, "Error: %s\n",
				    strerror(ret));
				(void) close(fd);
				return (-1);
			}

			(void) close(drv_fd);
		} else if (rflag) {
			int ret;

			io.fshthio_fd = fd;
			io.fshthio_arg = arg;

			drv_fd = open(FSHT_DEV_PATH, O_RDWR);
			if (drv_fd == -1) {
				perror("Error");
				(void) close(fd);
				return (-1);
			}

			ret = ioctl(drv_fd, FSHT_HOOKS_REMOVE, &io);
			if (ret != 0) {
				(void) fprintf(stderr, "Error: %s\n",
				    strerror(ret));
				(void) close(fd);
				return (-1);
			}

			(void) close(drv_fd);

		} else {
			(void) close(fd);
			goto usage;
		}

		(void) close(fd);
	} else if (cflag) {
		(void) fprintf(stderr, "Error: not supported yet\n");
		return (-1);
	} else {
		goto usage;
	}

	return (0);

usage:
	return (-1);
	/* TODO: print usage */

}
