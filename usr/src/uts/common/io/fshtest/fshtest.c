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
#include <sys/fsh_impl.h>
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
#include <sys/vfs.h>

#define	FSHT_MAGIC	(0xB06E1)

typedef struct fsht_arg {
	fsh_handle_t handle;
	int magic1;
	int magic2;
	int op;
} fsht_arg_t;

typedef struct fsht_int {
	fsh_handle_t	fshti_handle;
	int		fshti_doomed;
	fsht_arg_t	fshti_arg;
	list_node_t	fshti_node;
} fsht_int_t;

typedef struct fsht_cb_int {
	fsh_callback_handle_t fshtcbi_handle;
	int64_t		fshtcbi_arg;
	list_node_t	fshtcbi_next;
} fsht_cb_int_t;

static dev_info_t *fsht_devi;

/*
 * fsht_lock protects: fsht_detaching, fsht_hooks, fsht_hooks_count,
 * fsht_hooks_empty, fsht_enabled
 */
static kmutex_t fsht_lock;

static int fsht_detaching;

static list_t fsht_hooks;
static int fsht_hooks_count;
static kcondvar_t fsht_hooks_empty;

static kthread_t *fsht_owner;
static kmutex_t fsht_owner_lock;

static int fsht_enabled;

/* Testing hooks */
static void
pre_hook(void *arg1, void **instancepp)
{
	fsht_arg_t *arg = &((fsht_int_t *)arg1)->fshti_arg;

	switch (arg->op) {
	case FSHTT_DUMMY:
		break;

	case FSHTT_PREPOST:
		*instancepp = kmem_alloc(sizeof (int), KM_SLEEP);
		/* Verified in post hook */
		*(int *)*instancepp = arg->magic1;

		/* Added 1 in post hook, verified in remove callback */
		arg->magic2 = arg->magic1;
		break;

	case FSHTT_API: {
		fsh_handle_t handle;
		fsh_t hook = { 0 };
		fsh_callback_handle_t cb_handle;
		fsh_callback_t cb = { 0 };
		vfs_t *vfsp;

		vfsp = vfs_alloc(KM_SLEEP);

		if ((handle = fsh_hook_install(vfsp, &hook)) != -1)
			VERIFY(fsh_hook_remove(handle) == 0);

		if ((cb_handle = fsh_callback_install(&cb)) != -1)
			VERIFY(fsh_callback_remove(cb_handle) == 0);
		
		fsh_exec_mount_callbacks(vfsp);
		fsh_exec_free_callbacks(vfsp);

		/*
		 * fsh_fsrec_destroy() is called inside vfs_free()
		 */
		vfs_free(vfsp);

		break;
	}

	case FSHTT_AFTER_REMOVE:
		/*
		 * Set while installing.
		 * Remove callback sets magic2 to some sentinel value.
		 */
		VERIFY(arg->magic2 == arg->magic1);
		break;

	case FSHTT_SELF_DESTROY:
		(void) fsh_hook_remove(arg->handle);

		/* Post adds 1, remove callback verifies */
		arg->magic2 = arg->magic1;
		break;

	default:
		break;
	}


}

static void
post_hook(void *arg1, void *instancep)
{
	fsht_arg_t *arg = &((fsht_int_t *)arg1)->fshti_arg;

	switch (arg->op) {
	case FSHTT_DUMMY:
		break;

	case FSHTT_PREPOST:
		VERIFY(*(int *)instancep == arg->magic1);
		kmem_free(instancep, sizeof (int));

		VERIFY(arg->magic2 == arg->magic1);

		/* Verified to be 2 in remove callback */
		arg->magic2++;
		break;

	case FSHTT_API: {
		fsh_handle_t handle;
		fsh_t hook = { 0 };
		fsh_callback_handle_t cb_handle;
		fsh_callback_t cb = { 0 };
		vfs_t *vfsp;

		vfsp = vfs_alloc(KM_SLEEP);

		if ((handle = fsh_hook_install(vfsp, &hook)) != -1)
			VERIFY(fsh_hook_remove(handle) == 0);

		if ((cb_handle = fsh_callback_install(&cb)) != -1)
			VERIFY(fsh_callback_remove(cb_handle) == 0);

		fsh_exec_mount_callbacks(vfsp);
		fsh_exec_free_callbacks(vfsp);

		/*
		 * fsh_fsrec_destroy() is called inside vfs_free()
		 */
		vfs_free(vfsp);

		break;
	}

	case FSHTT_AFTER_REMOVE:
		/*
		 * Set before installing.
		 * Remove callback sets magic2 to some sentinel value.
		 */
		VERIFY(arg->magic2 == arg->magic1);
		break;

	case FSHTT_SELF_DESTROY:
		/* Remove callback sets magic2 to some sentinel value. */
		VERIFY(arg->magic2 == arg->magic1);
		/* Remove callback verifies */
		arg->magic2++;
		break;

	default:
		break;
	}
}

/*ARGSUSED*/
static void
fsht_pre_read(void *arg1, void **arg2, vnode_t **arg3, uio_t **arg4, int *arg5,
	cred_t **arg6, caller_context_t **arg7)
{
	pre_hook(arg1, arg2);
}

/*ARGSUSED*/
static int
fsht_post_read(int ret, void *arg1, void *arg2, vnode_t *arg3, uio_t *arg4,
	int arg5, cred_t *arg6, caller_context_t *arg7)
{
	post_hook(arg1, arg2);
	return (ret);
}

/*ARGSUSED*/
static void
fsht_pre_write(void *arg1, void **arg2, vnode_t **arg3, uio_t **arg4, int *arg5,
	cred_t **arg6, caller_context_t **arg7)
{
	pre_hook(arg1, arg2);
}

/*ARGSUSED*/
static int
fsht_post_write(int ret, void *arg1, void *arg2, vnode_t *arg3, uio_t *arg4,
	int arg5, cred_t *arg6, caller_context_t *arg7)
{
	post_hook(arg1, arg2);
	return (ret);
}

/* vfs */
/*ARGSUSED*/
static void
fsht_pre_mount(void *arg1, void **arg2, vfs_t **arg3, vnode_t **arg4,
	struct mounta **arg5, cred_t **arg6)
{
	pre_hook(arg1, arg2);
}

/*ARGSUSED*/
static int
fsht_post_mount(int ret, void *arg1, void *arg2, vfs_t *arg3, vnode_t *arg4,
	struct mounta *arg5, cred_t *arg6)
{
	post_hook(arg1, arg2);
	return (ret);
}

/*ARGSUSED*/
static void
fsht_pre_unmount(void *arg1, void **arg2, vfs_t **arg3, int *arg4,
	cred_t **arg5)
{
	pre_hook(arg1, arg2);
}

/*ARGSUSED*/
static int
fsht_post_unmount(int ret, void *arg1, void *arg2, vfs_t *arg3, int arg4,
	cred_t *arg5)
{
	post_hook(arg1, arg2);
	return (ret);
}


/* Hook remove callback */
static void
fsht_remove_cb(void *arg1, fsh_handle_t handle)
{
	_NOTE(ARGUNUSED(handle));

	fsht_int_t *fshti = (fsht_int_t *)arg1;
	fsht_arg_t *arg = &fshti->fshti_arg;
	int fsht_context;

	/* Tests */
	switch (arg->op) {
	case FSHTT_DUMMY:
		break;

	case FSHTT_PREPOST:
		VERIFY(arg->magic2 == arg->magic1 + 1);
		break;

	case FSHTT_API: {
		fsh_handle_t handle;
		fsh_t hook = { 0 };
		fsh_callback_handle_t cb_handle;
		fsh_callback_t cb = { 0 };
		vfs_t *vfsp;

		vfsp = vfs_alloc(KM_SLEEP);

		if ((handle = fsh_hook_install(vfsp, &hook)) != -1)
			VERIFY(fsh_hook_remove(handle) == 0);

		if ((cb_handle = fsh_callback_install(&cb)) != -1)
			VERIFY(fsh_callback_remove(cb_handle) == 0);

		fsh_exec_mount_callbacks(vfsp);
		fsh_exec_free_callbacks(vfsp);

		/*
		 * fsh_fsrec_destroy() is called inside vfs_free()
		 */
		vfs_free(vfsp);

		break;
	}

	case FSHTT_AFTER_REMOVE:
		arg->magic2 = arg->magic1 + 1;
		break;

	case FSHTT_SELF_DESTROY:
		VERIFY(arg->magic2 == arg->magic1 + 1);
		break;

	default:
		break;
	}


	/* Cleaning up */
	mutex_enter(&fsht_owner_lock);
	fsht_context = fsht_owner == curthread;
	mutex_exit(&fsht_owner_lock);

	if (!fsht_context)
		mutex_enter(&fsht_lock);

	if (!fsht_detaching)
		list_remove(&fsht_hooks, fshti);
	kmem_free(fshti, sizeof (*fshti));

	VERIFY(fsht_hooks_count > 0);
	fsht_hooks_count--;

	if (!fsht_context) {
		if (fsht_hooks_count == 0)
			cv_signal(&fsht_hooks_empty);
		mutex_exit(&fsht_lock);
	}
}

static int
fsht_hook_install(vfs_t *vfsp, int type, int arg, int64_t *handle)
{
	fsh_t hook = { 0 };
	fsht_int_t *fshti;

	fshti = kmem_zalloc(sizeof (*fshti), KM_SLEEP);

	switch (type) {
	case FSHTT_DUMMY:
		fshti->fshti_arg.magic1 = arg;
		break;

	case FSHTT_API:
		break;

	case FSHTT_AFTER_REMOVE:
		fshti->fshti_arg.magic2 = FSHT_MAGIC;
		/*FALLTHROUGH*/
	case FSHTT_PREPOST:
	case FSHTT_SELF_DESTROY:
		fshti->fshti_arg.magic1 = FSHT_MAGIC;
		break;

	default:
		return (EINVAL);
	}

	fshti->fshti_arg.op = type;

	hook.arg = fshti;

	hook.pre_read = fsht_pre_read;
	hook.pre_write = fsht_pre_write;
	hook.pre_mount = fsht_pre_mount;
	hook.pre_unmount = fsht_pre_unmount;

	hook.post_read = fsht_post_read;
	hook.post_write = fsht_post_write;
	hook.post_mount = fsht_post_mount;
	hook.post_unmount = fsht_post_unmount;

	hook.remove_cb = fsht_remove_cb;

	*handle = fshti->fshti_handle = fsh_hook_install(vfsp, &hook);
	fshti->fshti_arg.handle = *handle;
	if (fshti->fshti_handle == -1) {
		kmem_free(fshti, sizeof (*fshti));
		return (EAGAIN);
	}

	mutex_enter(&fsht_lock);
	list_insert_head(&fsht_hooks, fshti);
	fsht_hooks_count++;
	mutex_exit(&fsht_lock);

	return (0);
}

static int
fsht_hook_remove(fsh_handle_t handle)
{
	return (fsh_hook_remove(handle) == 0 ? 0 : ENOENT);
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

	fsht_enabled = 0;
	fsht_detaching = 0;

	fsht_hooks_count = 0;
	mutex_init(&fsht_lock, NULL, MUTEX_DRIVER, NULL);
	list_create(&fsht_hooks, sizeof (fsht_int_t),
	    offsetof(fsht_int_t, fshti_node));
	cv_init(&fsht_hooks_empty, NULL, CV_DRIVER, NULL);

	fsht_owner = NULL;
	mutex_init(&fsht_owner_lock, NULL, MUTEX_DRIVER, NULL);

	return (DDI_SUCCESS);
}

static int
fsht_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int enabled;
	fsht_int_t *fshti;

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	mutex_enter(&fsht_lock);
	enabled = fsht_enabled;
	mutex_exit(&fsht_lock);

	if (enabled)
		return (DDI_FAILURE);

	VERIFY(dip == fsht_devi);

	ddi_remove_minor_node(dip, NULL);
	fsht_devi = NULL;


	mutex_enter(&fsht_lock);

	fsht_detaching = 1;

	mutex_enter(&fsht_owner_lock);
	VERIFY(fsht_owner == NULL);
	fsht_owner = curthread;
	mutex_exit(&fsht_owner_lock);

	while ((fshti = list_remove_head(&fsht_hooks)) != NULL) {
		/* Since we have no free callback, there's no VERIFY() here */
		(void) fsh_hook_remove(fshti->fshti_handle);
	}

	mutex_enter(&fsht_owner_lock);
	VERIFY(fsht_owner == curthread);
	fsht_owner = NULL;
	mutex_exit(&fsht_owner_lock);
	
	/* Some hooks might still be running. */
	while (fsht_hooks_count > 0)
		cv_wait(&fsht_hooks_empty, &fsht_lock);

	mutex_exit(&fsht_lock);


	VERIFY(list_is_empty(&fsht_hooks));
	list_destroy(&fsht_hooks);
	
	mutex_destroy(&fsht_owner_lock);
	cv_destroy(&fsht_hooks_empty);
	mutex_destroy(&fsht_lock);

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

	int enabled;

	mutex_enter(&fsht_lock);
	enabled = fsht_enabled;
	mutex_exit(&fsht_lock);
	if (!enabled && cmd != FSHT_ENABLE) {
		*rvalp = ENOTACTIVE;
		return (0);
	}

	switch (cmd) {
	case FSHT_ENABLE: {
		mutex_enter(&fsht_lock);
		fsht_enabled = 1;
		mutex_exit(&fsht_lock);
		return (0);
	}
	case FSHT_DISABLE: {
		mutex_enter(&fsht_lock);
		fsht_enabled = 0;
		mutex_exit(&fsht_lock);
		return (0);
	}
	case FSHT_HOOK_INSTALL: {
		fsht_hook_ioc_t io;
		file_t *file;
		vfs_t *vfsp;

		if (ddi_copyin((void *)arg, &io, sizeof (io), mode))
			return (EFAULT);

		if ((file = getf((int)io.install.fshthio_fd)) == NULL) {
			*rvalp = EBADFD;
			return (0);
		}
		vfsp = file->f_vnode->v_vfsp;
		releasef((int)io.install.fshthio_fd);

		*rvalp = fsht_hook_install(vfsp, io.install.fshthio_type,
		    io.install.fshthio_arg, &io.out.fshthio_handle);

		if (ddi_copyout(&io, (void *)arg, sizeof (io), mode))
			return (EFAULT);

		return (0);
	}

	case FSHT_HOOK_REMOVE: {
		fsht_hook_ioc_t io;

		if (ddi_copyin((void *)arg, &io, sizeof (io), mode))
			return (EFAULT);

		*rvalp = fsht_hook_remove(io.remove.fshthio_handle);
		return (0);
	}

	case FSHT_CB_INSTALL:
		/* TODO */
		return (ENOTTY);

	case FSHT_CB_REMOVE:
		/* TODO */
		return (ENOTTY);

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
