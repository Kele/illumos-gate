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
#include <stdlib.h>
#include <sys/fshtest.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "common.h"

extern int errno;

int
fsht_open()
{
	int fd = open(FSHT_DEV_PATH, O_RDWR);
	return (fd);
}

void
fsht_close(int fd)
{
	(void) close(fd);
}

int
fsht_enable(int fd)
{
	return (ioctl(fd, FSHT_ENABLE));
}

int
fsht_disable(int fd)
{
	return (ioctl(fd, FSHT_DISABLE));
}


int64_t
fsht_hook_install(int fd, char *mnt, int type, int arg)
{
	fsht_hook_ioc_t ioc;
	int mntfd;
	int64_t ret = 0;

	ioc.install.fshthio_type = type;

	switch (type) {
	case FSHTT_DUMMY:
		ioc.install.fshthio_arg = arg;
		/*FALLTHROUGH*/
	case FSHTT_PREPOST:
		/*FALL THROUGH*/
	case FSHTT_API:
		/*FALL THROUGH*/
	case FSHTT_AFTER_REMOVE:
		/*FALL THROUGH*/
	case FSHTT_SELF_DESTROY:
		if ((mntfd = open(mnt, O_RDONLY)) == -1)
			return (-2);
		ioc.install.fshthio_fd = mntfd;	

		ioc.install.fshthio_type = type;
		ret = ioctl(fd, FSHT_HOOK_INSTALL, &ioc);
		ret = ioc.out.fshthio_handle;
		(void) close(mntfd);
		break;

	default:
		ret = -3;
		break;
	}

	return (ret);
}

int
fsht_hook_remove(int fd, int64_t handle)
{
	fsht_hook_ioc_t ioc;
	ioc.remove.fshthio_handle = handle;
	return (ioctl(fd, FSHT_HOOK_REMOVE, &ioc));
}
