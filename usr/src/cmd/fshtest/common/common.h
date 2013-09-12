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

#ifndef _FSHT_COMMON_H
#define	_FSHT_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#define	FSHTT_DUMMY		1
#define	FSHTT_PREPOST		2
#define	FSHTT_API		3
#define	FSHTT_AFTER_REMOVE	4
#define	FSHTT_SELF_DESTROY	5

int fsht_open();
void fsht_close(int fd);

int fsht_enable(int fd);
int fsht_disable(int fd);

int64_t fsht_hook_install(int fd, char *mnt, int type, int arg);
int fsht_hook_remove(int fd, int64_t handle);

#ifdef __cplusplus
}
#endif

#endif /* _FSHT_COMMON_H */
