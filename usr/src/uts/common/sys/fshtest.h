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

#ifndef _FSHTEST_H
#define	_FSHTEST_H

/*
 * filesystem disturber header file
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/errno.h>
#include <sys/ksynch.h>
#include <sys/list.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/vfs.h>


#define	FSHT_DEV_PATH	"/dev/fshtest"

#define	FSHT_IOC		(('f' | 's') << 24 | 'h' << 16 | 't' << 8)
#define	FSHT_HOOKS_INSTALL	(FSHT_IOC | 1)
#define	FSHT_HOOKS_REMOVE	(FSHT_IOC | 2)
#define	FSHT_CB_INSTALL		(FSHT_IOC | 3)
#define	FSHT_CB_REMOVE		(FSHT_IOC | 4)
#define	FSHT_ENABLE		(FSHT_IOC | 5)
#define	FSHT_DISABLE		(FSHT_IOC | 6)

typedef struct fsht_hook_ioc {
	int64_t	fshthio_fd;
	int64_t	fshthio_arg;	/* Displayed by DTrace */
} fsht_hook_ioc_t;

typedef struct fsht_cb_ioc {
	int64_t	fshtcio_arg;	/* Displayed by DTrace */
} fsht_cb_ioc_t;

#ifdef __cplusplus
}
#endif

#endif /* _FSHTEST_H */
