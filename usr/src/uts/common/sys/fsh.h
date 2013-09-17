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

#ifndef _FSH_H
#define	_FSH_H

#include <sys/id_space.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/vnode.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef id_t fsh_handle_t;
typedef id_t fsh_callback_handle_t;

typedef struct fsh {
	void *arg;
	void (*remove_cb)(void *, fsh_handle_t);

	/* vnode */
	void (*pre_read)(void *, void **, vnode_t **, uio_t **, int *,
		cred_t **, caller_context_t **);
	int (*post_read)(int, void *, void *, vnode_t *, uio_t *, int, cred_t *,
		caller_context_t *);
	void (*pre_write)(void *, void **, vnode_t **, uio_t **, int *,
		cred_t **, caller_context_t **);
	int (*post_write)(int, void *, void *, vnode_t *, uio_t *, int,
		cred_t *, caller_context_t *);

	/* vfs */
	void (*pre_mount)(void *, void **, vfs_t **, vnode_t **,
		struct mounta **, cred_t **);
	int (*post_mount)(int, void *, void *, vfs_t *, vnode_t *,
		struct mounta *, cred_t *);
	void (*pre_unmount)(void *, void **, vfs_t **, int *, cred_t **);
	int (*post_unmount)(int, void *, void *, vfs_t *, int, cred_t *);
} fsh_t;

typedef struct fsh_callback {
	void	*fshc_arg;
	void	(*fshc_free)(vfs_t *, void *);
	void	(*fshc_mount)(vfs_t *, void *);
} fsh_callback_t;

/* API */
extern fsh_handle_t fsh_hook_install(vfs_t *, fsh_t *);
extern int fsh_hook_remove(fsh_handle_t);

extern fsh_callback_handle_t fsh_callback_install(fsh_callback_t *);
extern int fsh_callback_remove(fsh_callback_handle_t);

#ifdef __cplusplus
}
#endif

#endif /* _FSH_H */
