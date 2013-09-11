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
 * Test driver for filesystem hook framework (fsh)
 */

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/file.h>
#include <sys/fsh.h>
#include <sys/fshtest.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <sys/list.h>
#include <sys/mkdev.h>
#include <sys/refstr.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

typedef struct fsht_int {
	fsh_handle_t	fshti_handle;
	int		fshti_doomed;
	int64_t		fshti_arg;
	vfs_t		*fshti_vfsp;
	list_node_t	fshti_next;
} fsht_int_t;

typedef struct fshtcb_int {
	fsh_callback_handle_t fshtcbi_handle;
	int64_t		fshtcbi_arg;
	list_node_t	fshtcbi_next;
} fshtcb_int_t;

static dev_info_t *fsht_devi;

static kmutex_t fsht_lock;
static list_t fsht_hooks;	/* list of fsht_int_t */
static list_t fsht_cbs;		/* list of fshtcb_int_t */

static kcondvar_t fsht_hooks_empty;

static kmutex_t fsht_owner_lock;
static kthread_t *fsht_owner;


/* Dummy hooks */
static int
fsht_read(fsh_int_t *fshi, void *arg, vnode_t *vp, uio_t *uiop, int ioflag,
    cred_t *cr, caller_context_t *ct)
{
	_NOTE(ARGUNUSED(arg));
	return (fsh_next_read(fshi, vp, uiop, ioflag, cr, ct));
}

static int
fsht_write(fsh_int_t *fshi, void *arg, vnode_t *vp, uio_t *uiop, int ioflag,
    cred_t *cr, caller_context_t *ct)
{
	_NOTE(ARGUNUSED(arg));
	return (fsh_next_write(fshi, vp, uiop, ioflag, cr, ct));
}

static int
fsht_mount(fsh_int_t *fshi, void *arg, vfs_t *vfsp, vnode_t *mvp,
    struct mounta *uap, cred_t *cr)
{
	_NOTE(ARGUNUSED(arg));
	return (fsh_next_mount(fshi, vfsp, mvp, uap, cr));
}

static int
fsht_unmount(fsh_int_t *fshi, void *arg, vfs_t *vfsp, int flag, cred_t *cr)
{
	_NOTE(ARGUNUSED(arg));
	return (fsh_next_unmount(fshi, vfsp, flag, cr));
}

/* Hook remove callback */
static void
fsht_cb_remove(void *arg, fsh_handle_t handle)
{
	_NOTE(ARGUNUSED(handle));

	int fsht_context;
	fsht_int_t *fshti = (fsht_int_t *)arg;

	mutex_enter(&fsht_owner_lock);
	fsht_context = fsht_owner == curthread;
	mutex_exit(&fsht_owner_lock);

	if (!fsht_context)
		mutex_enter(&fsht_lock);

	ASSERT(MUTEX_HELD(&fsht_lock));

	list_remove(&fsht_hooks, fshti);
	kmem_free(fshti, sizeof (*fshti));

	if (list_head(&fsht_hooks) == NULL)
		cv_signal(&fsht_hooks_empty);

	if (!fsht_context)
		mutex_exit(&fsht_lock);
}

static int
fsht_hook_install(vfs_t *vfsp, int64_t arg)
{
	fsht_int_t *fshti;
	fsh_t hook = { 0 };

	mutex_enter(&fsht_lock);

	for (fshti = list_head(&fsht_hooks); fshti != NULL;
	    fshti = list_next(&fsht_hooks, fshti)) {
		if (fshti->fshti_vfsp == vfsp && fshti->fshti_arg == arg)
			break;
	}

	if (fshti != NULL) {
		mutex_exit(&fsht_lock);
		return (EEXIST);
	}

	fshti = kmem_zalloc(sizeof (*fshti), KM_SLEEP);
	fshti->fshti_vfsp = vfsp;
	fshti->fshti_arg = arg;

	hook.arg = fshti;
	hook.read = fsht_read;
	hook.write = fsht_write;
	hook.mount = fsht_mount;
	hook.unmount = fsht_unmount;
	hook.remove_cb = fsht_cb_remove;

	fshti->fshti_handle = fsh_hook_install(vfsp, &hook);
	if (fshti->fshti_handle == -1) {
		kmem_free(fshti, sizeof (*fshti));
		mutex_exit(&fsht_lock);
		return (EAGAIN);
	}
	list_insert_head(&fsht_hooks, fshti);

	mutex_exit(&fsht_lock);

	return (0);
}

static int
fsht_hook_remove(vfs_t *vfsp, int64_t arg)
{
	fsht_int_t *fshti;

	mutex_enter(&fsht_lock);

	for (fshti = list_head(&fsht_hooks); fshti != NULL;
	    fshti = list_next(&fsht_hooks, fshti)) {
		if (fshti->fshti_vfsp == vfsp && fshti->fshti_arg == arg)
			break;
	}

	if (fshti == NULL) {
		mutex_exit(&fsht_lock);
		return (ENOENT);
	}

	mutex_enter(&fsht_owner_lock);
	fsht_owner = curthread;
	mutex_exit(&fsht_owner_lock);

	ASSERT(fsh_hook_remove(fshti->fshti_handle) == 0);

	mutex_enter(&fsht_owner_lock);
	fsht_owner = NULL;
	mutex_exit(&fsht_owner_lock);

	mutex_exit(&fsht_lock);

	return (0);
}

/* Dummy callbacks */
static void
fsht_cb_mount(vfs_t *vfsp, void *arg)
{
	_NOTE(ARGUNUSED(vfsp));
	_NOTE(ARGUNUSED(arg));
}

static void
fsht_cb_free(vfs_t *vfsp, void *arg)
{
	_NOTE(ARGUNUSED(vfsp));
	_NOTE(ARGUNUSED(arg));
}

static int
fsht_install_cb(int64_t arg)
{
	fsh_callback_t callback = { 0 };
	fshtcb_int_t *fshtcbi;

	mutex_enter(&fsht_lock);
	for (fshtcbi = list_head(&fsht_cbs); fshtcbi != NULL;
	    fshtcbi = list_next(&fsht_cbs, fshtcbi)) {
		if (fshtcbi->fshtcbi_arg == arg)
			break;
	}
	if (fshtcbi != NULL) {
		mutex_exit(&fsht_lock);
		return (EEXIST);
	}

	fshtcbi = kmem_zalloc(sizeof (*fshtcbi), KM_SLEEP);
	fshtcbi->fshtcbi_arg = arg;

	callback.fshc_arg = fshtcbi;
	callback.fshc_free = fsht_cb_free;
	callback.fshc_mount = fsht_cb_mount;

	fshtcbi->fshtcbi_handle = fsh_callback_install(&callback);
	if (fshtcbi->fshtcbi_handle) {
		kmem_free(fshtcbi, sizeof (*fshtcbi));
		mutex_exit(&fsht_lock);
		return (EAGAIN);
	}
	list_insert_head(&fsht_cbs, fshtcbi);

	mutex_exit(&fsht_lock);

	return (0);
}

static int
fsht_remove_cb(int64_t arg)
{
	fshtcb_int_t *fshtcbi;

	mutex_enter(&fsht_lock);
	for (fshtcbi = list_head(&fsht_cbs); fshtcbi != NULL;
	    fshtcbi = list_next(&fsht_cbs, fshtcbi)) {
		if (fshtcbi->fshtcbi_arg == arg)
			break;
	}
	if (fshtcbi == NULL) {
		mutex_exit(&fsht_lock);
		return (ENOENT);
	}

	ASSERT(fsh_callback_remove(fshtcbi->fshtcbi_handle) == 0);

	list_remove(&fsht_cbs, fshtcbi);
	kmem_free(fshtcbi, sizeof (*fshtcbi));

	mutex_exit(&fsht_lock);

	return (0);
}

/* Entry points */
static int
fsht_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	minor_t instance;

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (fsht_devi != NULL)
		return (DDI_FAILURE);

	instance = ddi_get_instance(dip);
	if (ddi_create_minor_node(dip, "fshtest", S_IFCHR, instance,
	    DDI_PSEUDO, 0) == DDI_FAILURE)
		return (DDI_FAILURE);
	fsht_devi = dip;
	ddi_report_dev(fsht_devi);

	mutex_init(&fsht_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&fsht_owner_lock, NULL, MUTEX_DRIVER, NULL);

	list_create(&fsht_hooks, sizeof (fsht_int_t),
	    offsetof(fsht_int_t, fshti_next));
	list_create(&fsht_cbs, sizeof (fshtcb_int_t),
	    offsetof(fshtcb_int_t, fshtcbi_next));

	cv_init(&fsht_hooks_empty, NULL, CV_DRIVER, NULL);

	return (DDI_SUCCESS);
}

static int
fsht_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	fsht_int_t *fshti;
	fshtcb_int_t *cb;

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ASSERT(dip == fsht_devi);

	ddi_remove_minor_node(dip, NULL);
	fsht_devi = NULL;

	mutex_enter(&fsht_lock);
	for (fshti = list_head(&fsht_hooks); fshti != NULL;
	    fshti = list_next(&fsht_hooks, fshti)) {
		if (fshti->fshti_doomed)
			continue;

		fshti->fshti_doomed = 1;

		mutex_enter(&fsht_owner_lock);
		fsht_owner = curthread;
		mutex_exit(&fsht_owner_lock);

		ASSERT(fsh_hook_remove(fshti->fshti_handle) == 0);

		mutex_enter(&fsht_owner_lock);
		fsht_owner = NULL;
		mutex_exit(&fsht_owner_lock);
	}

	while (list_head(&fsht_hooks) != NULL)
		cv_wait(&fsht_hooks_empty, &fsht_lock);
	
	while ((cb = list_remove_head(&fsht_cbs)) != NULL)
		ASSERT(fsh_callback_remove(cb->fshtcbi_handle) == 0);

	mutex_exit(&fsht_lock);
	mutex_destroy(&fsht_lock);
	mutex_destroy(&fsht_owner_lock);

	cv_destroy(&fsht_hooks_empty);

	list_destroy(&fsht_hooks);
	list_destroy(&fsht_cbs);

	return (DDI_SUCCESS);
}

static int
fsht_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **resultp)
{
	_NOTE(ARGUNUSED(dip));

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*resultp = fsht_devi;
		return (DDI_SUCCESS);
	case DDI_INFO_DEVT2INSTANCE:
		*resultp = (void *)(uintptr_t)getminor((dev_t)arg);
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

static int
fsht_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	_NOTE(ARGUNUSED(devp));

	if (flag & FEXCL || flag & FNDELAY)
		return (EINVAL);

	if (otyp != OTYP_CHR)
		return (EINVAL);

	if (!(flag & FREAD && flag & FWRITE))
		return (EINVAL);

	if (drv_priv(credp) == EPERM)
		return (EPERM);

	return (0);
}

static int
fsht_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	_NOTE(ARGUNUSED(dev));
	_NOTE(ARGUNUSED(flag));
	_NOTE(ARGUNUSED(otyp));
	_NOTE(ARGUNUSED(credp));

	return (0);
}

static int
fsht_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
	int *rvalp)
{
	_NOTE(ARGUNUSED(dev));
	_NOTE(ARGUNUSED(credp));

	switch (cmd) {
	case FSHT_HOOKS_INSTALL: {
		fsht_hook_ioc_t io;
		file_t *file;

		if (ddi_copyin((void *)arg, &io, sizeof (io), mode))
			return (EFAULT);

		if ((file = getf((int)io.fshthio_fd)) == NULL) {
			*rvalp = EBADFD;
			return (0);
		}

		*rvalp = fsht_hook_install(file->f_vnode->v_vfsp,
		    io.fshthio_arg);
		releasef((int)io.fshthio_fd);
		return (0);
	}
	case FSHT_HOOKS_REMOVE: {
		fsht_hook_ioc_t io;
		file_t *file;

		if (ddi_copyin((void *)arg, &io, sizeof (io), mode))
			return (EFAULT);

		if ((file = getf((int)io.fshthio_fd)) == NULL) {
			*rvalp = EBADFD;
			return (0);
		}

		*rvalp = fsht_hook_remove(file->f_vnode->v_vfsp,
		    io.fshthio_arg);
		releasef((int)io.fshthio_fd);
		return (0);
	}

	case FSHT_CB_INSTALL: {
		fsht_cb_ioc_t io;

		if (ddi_copyin((void *)arg, &io, sizeof (io), mode))
			return (EFAULT);

		*rvalp = fsht_install_cb(io.fshtcio_arg);
		return (0);
	}

	case FSHT_CB_REMOVE: {
		fsht_cb_ioc_t io;

		if (ddi_copyin((void *)arg, &io, sizeof (io), mode))
			return (EFAULT);
		*rvalp = fsht_remove_cb(io.fshtcio_arg);
		return (0);
	}

	default:
		return (ENOTTY);
	}
}

static struct cb_ops cb_ops = {
	fsht_open,	/* open(9E) */
	fsht_close,	/* close(9E) */
	nodev,		/* strategy(9E) */
	nodev,		/* print(9E) */
	nodev,		/* dump(9E) */
	nodev,		/* read(9E) */
	nodev,		/* write(9E) */
	fsht_ioctl,	/* ioctl(9E) */
	nodev,		/* devmap(9E) */
	nodev,		/* mmap(9E) */
	nodev,		/* segmap(9E) */
	nochpoll,	/* chpoll(9E) */
	ddi_prop_op,	/* prop_op(9E) */
	NULL,		/* streamtab(9E) */
	D_MP | D_64BIT,	/* cb_flag(9E) */
	CB_REV,		/* cb_rev(9E) */
	nodev,		/* aread(9E) */
	nodev,		/* awrite(9E) */
};

static struct dev_ops dev_ops = {
	DEVO_REV,		/* driver build version */
	0,			/* reference count */
	fsht_getinfo,		/* getinfo */
	nulldev,
	nulldev,		/* probe */
	fsht_attach,		/* attach */
	fsht_detach,		/* detach */
	nodev,
	&cb_ops,		/* cb_ops */
	NULL,			/* bus_ops */
	NULL,			/* power */
	ddi_quiesce_not_needed,	/* quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,	"Filesystem hook framework test driver", &dev_ops
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}
