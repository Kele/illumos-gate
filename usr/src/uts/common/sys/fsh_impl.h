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

/*
 * This file includes all the necessary declarations for vfs.c and vnode.c.
 */

#ifndef _FSH_IMPL_H
#define	_FSH_IMPL_H

#include <sys/fsh.h>
#include <sys/list.h>
#include <sys/pathname.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/vnode.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fsh_fshrecord;
typedef struct fsh_fsrecord fsh_fsrecord_t;

/* API for vnode.c and vfs.c only */
/* vnode.c */
extern int fsh_read(vnode_t *, uio_t *, int, cred_t *, caller_context_t *);
extern int fsh_write(vnode_t *, uio_t *, int, cred_t *, caller_context_t *);

/* vfs.c */
extern void fsh_init(void);
extern void fsh_exec_mount_callbacks(vfs_t *);
extern void fsh_exec_free_callbacks(vfs_t *);
extern void fsh_fsrec_destroy(fsh_fsrecord_t *volatile);

extern int fsh_mount(vfs_t *, vnode_t *, struct mounta *, cred_t *);
extern int fsh_unmount(vfs_t *, int, cred_t *);

#ifdef __cplusplus
}
#endif

#endif /* _FSH_IMPL_H */
