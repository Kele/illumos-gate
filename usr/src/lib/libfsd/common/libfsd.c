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
#include <libfsd.h>
#include <string.h>
#include <stropts.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/fsd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


/*
 * libfsd
 * Library used to drive the fsd pseudo-device driver.
 *
 * 1. Usage.
 * fsd_handle_t is used for every operation on the fsd driver. It is acquired by
 * a call to fsd_open(). Every handle must be released by calling fsd_close().
 *
 * A handle is a structure that contains data needed to drive the fsd and get
 * information about errors.
 *
 *
 * Basics:
 * A disturber is a hook for filesystem operations. There are different types of
 * disturbers, but the property that connects them is that EVERY program that
 * uses filesystem API should expect that kind of behaviour. Doing otherwise is
 * a bug and could lead to serious problems.
 * Omnipresent disturber is a hook that is being installed whenever a new vfs_t
 * is mounted.
 *
 *
 * Errors:
 * In almost all the functions (except fsd_close()) return value equal to -1
 * indicates that there was an error. Last error data is contained in the
 * handle in two fields: fsd_errno and errno.
 * fsd_errno could be one of EFSD_XXX. errno is nonzero if and only if fsd_errno
 * is set to EFSD_CANT_OPEN_DRIVER or EFSD_CANT_OPEN_MOUNTPOINT.
 * fsd_strerr() is used to get the error message from fsd_errno. errno should be
 * treated according to Intro(2).
 *
 * Handle management:
 * fsd_open()
 *	Returns a handle.
 *
 * fsd_close(handle)
 *	Destroys a handle.
 *
 *
 * Enabling/disabling fsd:
 * fsd_enable(handle)
 * fsd_disable(handle)
 *	When fsd is enabled, it cannot be detached from the system. Otherwise,
 *	it could be and all the information that the fsd contains could be lost.
 *	The client should keep the fsd enabled whenever he needs to. No
 *	operations on fsd could be made if it's not enabled.
 *
 *
 * Getting information:
 * fsd_get_info(handle, info)
 *	Used to retrieve information such as:
 *	* whether fsd is enabled (system-wide)
 *	* whether there is an omnipresent disturber installed
 *	* omnipresent disturber's parameters
 *	* count of disturbers installed
 *
 * fsd_get_list(handle, fslist, count)
 *	Gets a list of disturbers installed. count is the maximum count of
 *	entries that could be returned by the function to the user-allocated
 *	buffer.	count is changed to min(count, number of disturbers installed).
 *	There is no error returned if the number of disturbers installed exceeds
 *	the user-provided count. It just gets the maximal allowed amount of
 *	informaton.
 *
 * fsd_get_param(handle, path, param)
 *	Gets disturber parameters from a filesystem of the file pointed by the
 *	path.
 *
 *
 * Installing/removing disturbers:
 * fsd_disturb(handle, path, param)
 *	Installs (or changes, if a disturber on this filesystem already exists)
 *	a disturber on a filesystem of the file pointed by the path.
 *
 * fsd_disturb_omni(handle, param)
 *	Installs (or changes, if an omnipresent disturber already exists) an
 *	omnipresent disturber. Only one omnipresent disturber exists at a time.
 *
 * fsd_disturb_off(handle, path)
 * fsd_disturb_omni_off(handle)
 *	Removes a disturber. It is guaranteed that after this function returns,
 *	the disturber won't change the behaviour of filesystem operations.
 *
 * 2. Multithreading.
 * It is safe to use the libfsd API concurrently.
 *
 * Error information is encapsulated in a handle, so it's the client's job to
 * properly share the handle between threads to preserve the consistency of
 * error data within a handle.
 */

extern int errno;

const char *
fsd_strerr(int e)
{
	switch (e) {
	case EFSD_NOERROR:
		return ("no error");
	case EFSD_BAD_PARAM:
		return ("incorrect disturbance parameters");
	case EFSD_INTERNAL:
		return ("internal library error");
	case EFSD_NOT_ENABLED:
		return ("fsd is not enabled");
	case EFSD_CANT_OPEN_DRIVER:
		return ("cannot open fsd device");
	case EFSD_CANT_OPEN_MOUNTPOINT:
		return ("cannot open mountpoint");
	case EFSD_ENTRY_NOT_FOUND:
		return ("this filesystem is not being disturbed");
	case EFSD_FAULT:
		return ("bad pointer");
	case EFSD_TOO_MANY_HOOKS:
		return ("too many hooks");
	case EFSD_UNKNOWN_ERROR:
	default:
		return ("unknown error");
	}
}

static int
xlate_errno(int e)
{
	switch (e) {
	case 0:
		return (0);
	case (-1):
		switch (errno) {
		case 0:
			return (EFSD_NOERROR);
		case EFAULT:
			return (EFSD_FAULT);
		case ENOTTY:
			return (EFSD_INTERNAL);
		default:
			return (EFSD_UNKNOWN_ERROR);
		}
	case ENOTACTIVE:
		return (EFSD_NOT_ENABLED);
	case ENOENT:
		return (EFSD_ENTRY_NOT_FOUND);
	case EINVAL:
		return (EFSD_BAD_PARAM);
	case EBADFD:
		return (EFSD_INTERNAL);
	case EAGAIN:
		return (EFSD_TOO_MANY_HOOKS);
	default:
		return (EFSD_UNKNOWN_ERROR);
	}
}

static int
ioctl_set_handle(fsd_handle_t *handle, int ioctlret)
{
	handle->fsd_errno = xlate_errno(ioctlret);
	handle->_errno = 0;

	if (handle->fsd_errno == 0)
		return (0);
	else
		return (-1);
}

int
fsd_open(fsd_handle_t *handle)
{
	if ((handle->fd = open(FSD_DEV_PATH, O_RDWR)) == -1) {
		handle->fsd_errno = EFSD_CANT_OPEN_DRIVER;
		handle->_errno = errno;
		return (-1);
	}
	return (0);
}

void
fsd_close(fsd_handle_t *handle)
{
	(void) close(handle->fd);
	handle->fd = -1;
}


int
fsd_enable(fsd_handle_t *handle)
{
	return (ioctl_set_handle(handle, ioctl(handle->fd, FSD_ENABLE)));

}

int
fsd_disable(fsd_handle_t *handle)
{
	return (ioctl_set_handle(handle, ioctl(handle->fd, FSD_DISABLE)));
}

int
fsd_disturb(fsd_handle_t *handle, const char *mnt_path, fsd_t *param)
{
	fsd_ioc_t ioc;
	int error;

	(void) memcpy(&ioc.fsdioc_dis.fsdd_param, param,
	    sizeof (ioc.fsdioc_dis.fsdd_param));

	if ((ioc.fsdioc_dis.fsdd_mnt = open(mnt_path, O_RDONLY)) == -1) {
		handle->fsd_errno = EFSD_CANT_OPEN_MOUNTPOINT;
		handle->_errno = errno;
		return (-1);
	}

	error = ioctl(handle->fd, FSD_DISTURB, &ioc);
	(void) close(ioc.fsdioc_dis.fsdd_mnt);
	return (ioctl_set_handle(handle, error));
}

int
fsd_disturb_off(fsd_handle_t *handle, const char *mnt_path)
{
	fsd_ioc_t ioc;
	int error;

	if ((ioc.fsdioc_mnt = open(mnt_path, O_RDONLY)) == -1) {
		handle->fsd_errno = EFSD_CANT_OPEN_MOUNTPOINT;
		handle->_errno = errno;
		return (-1);
	}

	error = ioctl(handle->fd, FSD_DISTURB_OFF, &ioc);
	(void) close(ioc.fsdioc_mnt);
	return (ioctl_set_handle(handle, error));
}

int
fsd_disturb_omni(fsd_handle_t *handle, fsd_t *param)
{
	fsd_ioc_t ioc;

	(void) memcpy(&ioc.fsdioc_param, param, sizeof (ioc.fsdioc_param));

	return (ioctl_set_handle(handle, ioctl(handle->fd, FSD_DISTURB_OMNI,
	    &ioc)));
}

int
fsd_disturb_omni_off(fsd_handle_t *handle)
{
	return (ioctl_set_handle(handle, ioctl(handle->fd,
	    FSD_DISTURB_OMNI_OFF)));
}

int
fsd_get_param(fsd_handle_t *handle, const char *mnt_path, fsd_t *param)
{
	fsd_ioc_t ioc;
	int error;
	int mntfd;

	if ((ioc.fsdioc_mnt = open(mnt_path, O_RDONLY)) == -1) {
		handle->fsd_errno = EFSD_CANT_OPEN_MOUNTPOINT;
		handle->_errno = errno;
		return (-1);
	}
	mntfd = ioc.fsdioc_mnt;

	error = ioctl(handle->fd, FSD_GET_PARAM, &ioc);
	if (error == 0)
		(void) memcpy(param, &ioc.fsdioc_param, sizeof (*param));

	(void) close(mntfd);

	return (ioctl_set_handle(handle, error));
}

int
fsd_get_info(fsd_handle_t *handle, fsd_info_t *info)
{
	fsd_ioc_t ioc;
	int error;

	error = ioctl(handle->fd, FSD_GET_INFO, &ioc);
	if (error == 0)
		(void) memcpy(info, &ioc.fsdioc_info, sizeof (*info));

	return (ioctl_set_handle(handle, error));
}

int
fsd_get_list(fsd_handle_t *handle, fsd_fs_t *fslist, int *count)
{
	fsd_ioc_t ioc;
	int error;

	ioc.fsdioc_list.listp = (uintptr_t)fslist;
	ioc.fsdioc_list.count = *count;

	error = ioctl(handle->fd, FSD_GET_LIST, &ioc);
	*count = ioc.fsdioc_list.count;
	return (ioctl_set_handle(handle, error));
}
