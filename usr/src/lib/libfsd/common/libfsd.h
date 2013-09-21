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
 * Copyright 2013 Damian Bogel. All rights reserved.
 */

#ifndef _LIBFSD_H
#define	_LIBFSD_H

#include <sys/fsd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fsd_handle {
	int fd;
	int fsd_errno;
	int _errno;
} fsd_handle_t;

#define	EFSD_NOERROR			0
#define	EFSD_BAD_PARAM			1
#define	EFSD_CANT_OPEN_DRIVER		2
#define	EFSD_CANT_OPEN_MOUNTPOINT	3
#define	EFSD_ENTRY_NOT_FOUND		4
#define	EFSD_FAULT			5
#define	EFSD_NOT_ENABLED		6
#define	EFSD_TOO_MANY_HOOKS		7
#define	EFSD_INTERNAL			8
#define	EFSD_UNKNOWN_ERROR		9

extern const char *fsd_strerr(int e);

extern int fsd_open(fsd_handle_t *handle);
extern void fsd_close(fsd_handle_t *handle);

extern int fsd_enable(fsd_handle_t *handle);
extern int fsd_disable(fsd_handle_t *handle);

extern int fsd_get_disturber(fsd_handle_t *handle, const char *mnt_path,
		fsd_t *param);
extern int fsd_disturb(fsd_handle_t *handle, const char *mnt_path,
		fsd_t *param);
extern int fsd_disturb_off(fsd_handle_t *handle, const char *mnt_path);

extern int fsd_disturb_omni(fsd_handle_t *handle, fsd_t *param);
extern int fsd_disturb_omni_off(fsd_handle_t *handle);

extern int fsd_get_info(fsd_handle_t *handle, fsd_info_t *info);
extern int fsd_get_list(fsd_handle_t *handle, fsd_fs_t *fslist, int *count);

#ifdef __cplusplus
}
#endif

#endif /* _LIBFSD_H */
