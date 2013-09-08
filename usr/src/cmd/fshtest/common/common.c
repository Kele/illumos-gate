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

#include "common.h"

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


int 
fsht_install_hook(int fd, char *mnt, int arg)
{
	fsht_hook_ioc_t ioc;
	int mntfd, ret;

	ioc.fshthio_arg = arg;
	mntfd = open(mnt, O_RDONLY);
	if (mntfd == -1)
		return (-1);

	ioc.fshthio_fd = mntfd;
	ret = (ioctl(fd, FSHT_HOOKS_INSTALL, &ioc));

	(void) close(mntfd);
	return (ret);
}

int 
fsht_remove_hook(int fd, char *mnt, int arg)
{
	fsht_hook_ioc_t ioc;
	int mntfd, ret;

	ioc.fshthio_arg = arg;
	mntfd = open(mnt, O_RDONLY);
	if (mntfd == -1)
		return (-1);

	ioc.fshthio_fd = mntfd;
	ret = (ioctl(fd, FSHT_HOOKS_REMOVE, &ioc));

	(void) close(mntfd);
	return (ret);
}


int 
fsht_install_callback(int fd, int arg)
{
	fsht_cb_ioc_t ioc;

	ioc.fshtcio_arg = arg;
	return (ioctl(fd, FSHT_CB_INSTALL, &ioc));
}

int 
fsht_remove_callback(int fd, int arg)
{
	fsht_cb_ioc_t ioc;

	ioc.fshtcio_arg = arg;
	return (ioctl(fd, FSHT_CB_REMOVE, &ioc));
}

/* hooks */
void
linit(hook_list_t *list)
{
	list->count = 0;
	list->head = NULL;
}

hook_t *
lhead(hook_list_t *list)
{
	return (list->head);
}

hook_t *
lnext(hook_t *node)
{
	return (node->next);
}

hook_t *
lprev(hook_t *node)
{
	return (node->prev);
}

void
linsert_head(hook_list_t *list, int val)
{
	hook_t *node = malloc(sizeof (hook_t));

	node->val = val;
	node->prev = NULL;
	node->next = list->head;

	if (list->head)
		list->head->prev = node;
	list->head = node;
	list->count++;
}

void
lremove(hook_list_t *list, hook_t *node)
{
	if (list->head == node)
	{
		list->head = list->head->next;
		if (list->head != NULL)
			list->head->prev = NULL;
	} else {
		node->prev->next = node->next;
		if (node->next != NULL)
			node->next->prev = node->prev;
	}
	list->count--;
	free(node);
}

void
lremove_head(hook_list_t *list)
{
	hook_t *node = list->head;

	if (list->head) {
		list->head = list->head->next;
		if (list->head)
			list->head->prev = NULL;
		list->count--;
	}
	free(node);
}

int
lcount(hook_list_t *list)
{
	return (list->count);
}

void
lclear(hook_list_t *list)
{
	while (list->head != NULL)
		lremove_head(list);
}
