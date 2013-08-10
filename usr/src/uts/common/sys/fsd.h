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

#ifndef _FSD_H
#define	_FSD_H

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


#define	FSD_DEV_PATH	"/dev/fsd"

/*
 * Commands for ioctl().
 */
#define	FSDIOC	(('f' << 24) | ('s' << 16) | ('d' << 8))
#define	FSD_GET_PARAM		(FSDIOC | 1)
#define	FSD_ENABLE		(FSDIOC | 2)
#define	FSD_DISABLE		(FSDIOC | 3)
#define	FSD_DISTURB		(FSDIOC | 4)
#define	FSD_DISTURB_OFF		(FSDIOC | 5)
#define	FSD_DISTURB_OMNI	(FSDIOC | 6)
#define	FSD_DISTURB_OMNI_OFF	(FSDIOC | 7)
#define	FSD_GET_LIST		(FSDIOC | 8)
#define	FSD_GET_INFO		(FSDIOC | 9)


/*
 * Parameters description:
 * "read less"
 *	Makes a VOP_READ() call read n (from a given range) bytes less than it
 *	was requested.
 */
typedef struct fsd {
	uint64_t	read_less_chance;
	uint64_t	read_less_r[2];	/* range */
} fsd_t;

typedef struct fsd_info {
	uint64_t	fsdinf_enabled;		/* fsd enabled */
	uint64_t	fsdinf_count;		/* disturbers installed */
	uint64_t	fsdinf_omni_on;		/* omnipresent disturber on */
	fsd_t		fsdinf_omni_param;	/* omnipresent dist. params */
} fsd_info_t;

typedef struct fsd_dis {
	int64_t	fsdd_mnt;
	fsd_t	fsdd_param;
} fsd_dis_t;

typedef struct fsd_fs {
	fsd_t	fsdf_param;
	uint8_t	fsdf_name[MAXPATHLEN];
} fsd_fs_t;

typedef union fsd_ioc {		/* Used with:				*/
	fsd_info_t fsdioc_info;	/* _GET_INFO 				*/
	fsd_dis_t fsdioc_dis;	/* _DISTURB 				*/
	fsd_t	fsdioc_param;	/* _GET_PARAM out, _DISTURB_OMNI	*/
	int64_t	fsdioc_mnt;	/* _DISTURB_OFF, _GET_PARAM in		*/
	struct {
		int64_t	count;
		uint64_t listp;
	} fsdioc_list;		/* _GET_LIST				*/
} fsd_ioc_t;

#ifdef __cplusplus
}
#endif

#endif /* _FSD_H */
