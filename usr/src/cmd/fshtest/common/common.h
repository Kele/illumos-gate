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

int fsht_open();
void fsht_close(int fd);

int fsht_enable(int fd);
int fsht_disable(int fd);

int fsht_install_hook(int fd, char *mnt, int arg);
int fsht_remove_hook(int fd, char *mnt, int arg);

int fsht_install_callback(int fd, int arg);
int fsht_remove_callback(int fd, int arg);

typedef struct hook {
	int 		val;
	struct hook 	*next;
	struct hook	*prev;
} hook_t;

typedef struct hook_list {
	int count;
	hook_t *head;
} hook_list_t;

void linit(hook_list_t *);
hook_t *lhead(hook_list_t *);
hook_t *lnext(hook_t *);
hook_t *lprev(hook_t *);
void linsert_head(hook_list_t *, int);
void lremove(hook_list_t *, hook_t *);
void lremove_head(hook_list_t *);
int lcount(hook_list_t *);
void lclear(hook_list_t *);

#ifdef __cplusplus
}
#endif

#endif /* _FSHT_COMMON_H */
