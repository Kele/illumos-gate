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

/*
 * Filesystem disturber pseudo-device driver.
 */

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/file.h>
#include <sys/fsd.h>
#include <sys/fsh.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <sys/list.h>
#include <sys/mkdev.h>
#include <sys/refstr.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

/*
 * TODO:
 * - add checking if a file descriptor passed by the client is indeed
 *	a mountpoint (we'd like to avoid disturbing / instead of an
 *	unmounted filesystem)
 */
/*
 * fsd - filesystem disturber
 *
 * 1. Abstract
 * Filesystem disturber is a pseudo-device driver used to inject pathological
 * behaviour into vfs calls. It is NOT a fuzzer. That kind of behaviour
 * should be expected and correctly handled by software. A simple example of
 * such behaviour is read() reading less bytes than it was requested. It's
 * well documented and every read() caller should check the return value of
 * this function before proceeding.
 *
 * 2. Features
 * * per-vfs injections
 * * injection installing on every newly mounted vfs (that's called an
 *   omnipresent disturber)
 *
 * 3. Usage
 * fsd_t is a structure which contains all the parameters for the disturbers.
 * This structure is shared by all hooks on a vfs_t.
 *
 * fsd_info_t is filled out by a call to ioctl() and it provides basic
 * information about fsd's current status.
 *
 * fsd_dis_t is passed to ioctl() when a request to disturb a filesystem is
 * made. It's just a descriptor of a representative file and an fsd_t structure.
 *
 * fsd_fs_t is a structure filled out by ioctl() call when the client requests a
 * full list of disturbers installed in the system.
 *
 * fsd_ioc_t is an union for different ioctl() commands.
 *
 * ioctl() commands:
 * FSD_ENABLE:
 *	ioctl(fd, FSD_ENABLE);
 *	Enables the fsd. When fsd is enabled, any attemps to detach the driver
 *	will fail.
 *
 * FSD_DISABLE:
 *	ioctl(fd, FSD_DISABLE);
 *	Disables the fsd.
 *
 * FSD_GET_PARAM:
 *	ioctl(fd, FSD_GET_PARAM, ioc);
 *	Get's fsd_t associated with a given filesystem. ioc is fsdioc_mnt when
 *	passed to ioctl(). fsdioc_param is the output.
 *	Errors:
 *		ENOENT - the filesystem is not being disturbed
 *
 * FSD_DISTURB:
 *	ioctl(fd, FSD_DISTURB, ioc);
 *	Installs a disturber on a given filesystem. If a disturber is already
 *	installed on this filesystem, it overwrites it. ioc is fsdioc_dis.
 *	Errors:
 *		EAGAIN - hook limit exceeded
 *		EBADFD - cannot open the file descriptor
 *		EINVAL - parameters are invalid
 *
 * FSD_DISTURB_OFF:
 *	ioctl(fd, FSD_DISTURB_OFF, ioc);
 *	Removes a disturber from a given filesystem. ioc is fsdioc_mnt
 *	Errors:
 *		EBADFD - cannot open the file descriptor
 *		ENOENT - the filesystem is not being disturbed
 *
 * FSD_DISTURB_OMNI:
 *	ioctl(fd, FSD_DISTURB_OMNI, ioc);
 *	Install an omnipresent disturber. It means that whenever a new vfs_t is
 *	being created, this disturber is installed on it. If an omnipresent
 *	disturber is already installed, it overwrites it. ioc is fsdioc_param
 *	Errors:
 *		EINVAL - parameters are invalid
 *
 * FSD_DISTURB_OMNI_OFF:
 *	ioctl(fd, FSD_DISTURB_OMNI_OFF);
 *	Removes the omnipresent disturber. That does NOT mean that filesystems
 *	which are disturbed because of the omnipresent disturber presence in the
 *	past are going to stop being disturbed after this call.
 *
 * FSD_GET_LIST:
 *	ioctl(fd, FSD_GET_LIST, ioc);
 *	Get's a full list of disturbers installed in the system. ioc is
 *	fsdioc_list here. This is a structure with two fields, count and listp.
 *	The count is the number of fsd_fs_t's allocated on the address that
 *	listp is pointing to. There would be at most count fsd_fs_t entries
 *	copied out to the caller. Also, count is set to the number of entries
 *	copied out.
 *
 * FSD_GET_INFO:
 *	ioctl(fd, FSD_GET_INFO, ioc);
 *	Get's current information about fsd. ioc is fsdioc_info here.
 *
 * At most one hook is installed per vfs_t, and fsd_t describes all possible
 * disturbance methods. Multiple commands using the fsd should somehow cooperate
 * in order not to destroy each other efforts in installing disturbers.
 *
 * 4. Internals
 * When fsd_enabled is nonzero, fsd_detach() fails.
 *
 * These mount callback is used for installing injections on newly mounted
 * vfs_t's (omnipresent). The free callback is used for cleaning up.
 *
 * The list of currently installed hooks is kept in fsd_list.
 *
 * fsd installs at most one hook on a vfs_t.
 *
 * Inside fsd_detach, we go through fsd_hooks list. There is no guarantee that
 * a hook remove callback (fsd_remove_cb) wouldn't execute inside
 * fsh_hook_remove(), thus we can't assume that while walking through fsd_hooks,
 * our iterator will be valid, because fsh_hook_remove() could invalidate it.
 * That's why fsd_detaching flag is introduced.
 *
 * 5. Locking
 * Every modification of fsd_enable, fsd_hooks, fsd_omni_param and fsd_list is
 * protected by fsd_lock.
 *
 * Hooks use only the elements of fsd_list, nothing else. Before an element of
 * fsd_list is destroyed, a hook which uses it is removed. Elements from
 * fsd_lists are removed and destroyed in the hook remove callback
 * (fsd_remove_cb).
 *
 * Because of the fact that fsd_remove_cb() could be called both in the context
 * of the thread that executes fsh_hook_remove() or outside the fsd, we need to
 * use fsd_rem_thread in order not to cause a deadlock. fsh_hook_remove() could
 * be called by at most one thread inside fsd (fsd_disturber_remove() holds
 * fsd_lock). We just have to check inside fsd_remove_cb() if it was called
 * from fsh_hook_remove() or not. We use fsd_rem_thread to determine that.
 *
 * fsd_int_t.fsdi_param is protected by fsd_int_t.fsdi_lock which is an rwlock.
 */

/*
 * Once a set of hooks is installed on a filesystem, there's no need
 * to bother fsh if we want to change the parameters of disturbance.
 * Intead, we use fsd_lock to protect the fsd_int_t when it's being
 * used or changed.
 */
typedef struct fsd_int {
	krwlock_t	fsdi_lock;	/* protects fsd_param */
	fsd_t		fsdi_param;
	fsh_handle_t	fsdi_handle;	/* we use fsh's handle in fsd */
	vfs_t		*fsdi_vfsp;
	int		fsdi_doomed;
	list_node_t	fsdi_node;
} fsd_int_t;

static dev_info_t *fsd_devi;


/*
 * fsd_lock protects: fsd_enabled, fsd_omni_param, fsd_list, fsd_cb_handle,
 * fsd_detaching
 */
static kmutex_t fsd_lock;

static kthread_t *fsd_rem_thread;
static kmutex_t fsd_rem_thread_lock;

static fsd_t *fsd_omni_param;	/* Argument used by fsd's mount callback. */
static fsh_callback_handle_t fsd_cb_handle;
static int fsd_enabled;
static int fsd_detaching;

/*
 * List of fsd_int_t. For every vfs_t on which fsd has installed a set of hooks
 * there exist exactly one fsd_int_t with fsdi_vfsp pointing to this vfs_t.
 */
static list_t fsd_list;
static int fsd_list_count;
static kcondvar_t fsd_cv_empty;


/*
 * Although it's safe to use this kind of pseudo-random number generator,
 * it behaves very regular when it comes to parity. Every fsd_rand() call
 * changes the parity of the seed. That's why when a range of width 2 is set
 * as a parameter, it's highly possible that the random value will always be
 * the same, because fsd_rand() could be called the same number of times in a
 * hook.
 */
static long	fsd_rand_seed;

static int
fsd_rand()
{
	fsd_rand_seed = fsd_rand_seed * 1103515245L + 12345;
	return (fsd_rand_seed & 0x7ffffffff);
}

/* vnode hooks */
/*
 * A pointer to a given fsd_int_t is valid always inside fsh_hook_xxx()
 * call, because it's valid until the hooks associated with it are removed.
 * If a hook is removed, it cannot be executing.
 */
static void
fsd_hook_pre_read(void *arg, void **instancep, vnode_t **vpp, uio_t **uiopp,
	int *ioflagp, cred_t **crp, caller_context_t **ctp)
{
	_NOTE(ARGUNUSED(ioflagp));
	_NOTE(ARGUNUSED(crp));
	_NOTE(ARGUNUSED(ctp));

	fsd_int_t *fsdi = (fsd_int_t *)arg;
	uint64_t less_chance;

	/*
	 * It is used to keep an odd number of fsd_rand() calls in every
	 * fsd_hook_pre_read() call. That is desired because when a range of
	 * width 2 is set as a parameter, we don't want to make it a constant.
	 * The pseudo-random number generator returns a number with different
	 * parity with every call. If this function is called in every
	 * fsd_hook_pre_read() execution even number of times, it would always
	 * be the same % 2.
	 */
	(void) fsd_rand();

	ASSERT((*vpp)->v_vfsp == fsdi->fsdi_vfsp);

	rw_enter(&fsdi->fsdi_lock, RW_READER);
	less_chance = fsdi->fsdi_param.read_less_chance;
	rw_exit(&fsdi->fsdi_lock);

	if ((uint64_t)fsd_rand() % 100 < less_chance) {
		extern size_t copyout_max_cached;
		uint64_t r[2];
		uint64_t count, less;

		count = (*uiopp)->uio_iov->iov_len;
		r[0] = fsdi->fsdi_param.read_less_r[0];
		r[1] = fsdi->fsdi_param.read_less_r[1];
		less = (uint64_t)fsd_rand() % (r[1] + 1 - r[0]) + r[0];

		if (count > less) {
			count -= less;
			*instancep = kmem_alloc(sizeof (uint64_t), KM_SLEEP);
			*(*(uint64_t **)instancep) = less;
		} else {
			*instancep = NULL;
			return;
		}

		(*uiopp)->uio_iov->iov_len = count;
		(*uiopp)->uio_resid = count;
		if (count <= copyout_max_cached)
			(*uiopp)->uio_extflg = UIO_COPY_CACHED;
		else
			(*uiopp)->uio_extflg = UIO_COPY_DEFAULT;
	} else {
		*instancep = NULL;
	}
}

static int
fsd_hook_post_read(int ret, void *arg, void *instance, vnode_t *vp,
	uio_t *uiop, int oflag, cred_t *cr, caller_context_t *ct)
{
	_NOTE(ARGUNUSED(arg));
	_NOTE(ARGUNUSED(vp));
	_NOTE(ARGUNUSED(oflag));
	_NOTE(ARGUNUSED(cr));
	_NOTE(ARGUNUSED(ct));

	if (instance != NULL) {
		uint64_t *lessp = instance;
		uiop->uio_resid += *lessp;
		kmem_free(lessp, sizeof (*lessp));
	}
	return (ret);
}

static void
fsd_remove_cb(void *arg, fsh_handle_t handle)
{
	_NOTE(ARGUNUSED(handle));

	fsd_int_t *fsdi = (fsd_int_t *)arg;
	int fsd_context;

	mutex_enter(&fsd_rem_thread_lock);
	fsd_context = fsd_rem_thread == curthread;
	mutex_exit(&fsd_rem_thread_lock);

	if (!fsd_context)
		mutex_enter(&fsd_lock);

	ASSERT(MUTEX_HELD(&fsd_lock));

	if (!fsd_detaching)
		list_remove(&fsd_list, fsdi);

	rw_destroy(&fsdi->fsdi_lock);
	kmem_free(fsdi, sizeof (*fsdi));

	fsd_list_count--;
	if (fsd_list_count == 0)
		cv_signal(&fsd_cv_empty);

	if (!fsd_context)
		mutex_exit(&fsd_lock);
}

/*
 * Installs a set of hook with given parameters on a vfs_t.
 *
 * It is expected that fsd_lock is being held.
 *
 * Returns 0 on success and non-zero if hook limit exceeded.
 */
static int
fsd_disturber_install(vfs_t *vfsp, fsd_t *fsd)
{
	fsd_int_t *fsdi;

	ASSERT(MUTEX_HELD(&fsd_lock));

	for (fsdi = list_head(&fsd_list); fsdi != NULL;
	    fsdi = list_next(&fsd_list, fsdi)) {
		if (fsdi->fsdi_vfsp == vfsp)
			break;
	}

	if (fsdi != NULL) {
		/* Just change the existing fsd_int_t */
		rw_enter(&fsdi->fsdi_lock, RW_WRITER);
		(void) memcpy(&fsdi->fsdi_param, fsd,
		    sizeof (fsdi->fsdi_param));
		rw_exit(&fsdi->fsdi_lock);
	} else {
		fsh_t hook = { 0 };

		fsdi = kmem_zalloc(sizeof (*fsdi), KM_SLEEP);
		fsdi->fsdi_vfsp = vfsp;
		(void) memcpy(&fsdi->fsdi_param, fsd,
		    sizeof (fsdi->fsdi_param));
		rw_init(&fsdi->fsdi_lock, NULL, RW_DRIVER, NULL);

		hook.arg = fsdi;
		hook.pre_read = fsd_hook_pre_read;
		hook.post_read = fsd_hook_post_read;
		hook.remove_cb = fsd_remove_cb;

		/*
		 * It is safe to do so, because none of the hooks installed
		 * by fsd uses fsdi_handle nor the fsd_list.
		 */
		fsdi->fsdi_handle = fsh_hook_install(vfsp, &hook);
		if (fsdi->fsdi_handle == -1) {
			kmem_free(fsdi, sizeof (*fsdi));
			rw_destroy(&fsdi->fsdi_lock);
			return (-1);
		}
		list_insert_head(&fsd_list, fsdi);
		fsd_list_count++;
	}
	return (0);
}

static int
fsd_disturber_remove(vfs_t *vfsp)
{
	fsd_int_t *fsdi;

	ASSERT(MUTEX_HELD(&fsd_lock));

	for (fsdi = list_head(&fsd_list); fsdi != NULL;
	    fsdi = list_next(&fsd_list, fsdi)) {
		if (fsdi->fsdi_vfsp == vfsp)
			break;
	}
	if (fsdi == NULL || fsdi->fsdi_doomed)
		return (ENOENT);

	fsdi->fsdi_doomed = 1;

	mutex_enter(&fsd_rem_thread_lock);
	fsd_rem_thread = curthread;
	mutex_exit(&fsd_rem_thread_lock);

	ASSERT(fsh_hook_remove(fsdi->fsdi_handle) == 0);

	mutex_enter(&fsd_rem_thread_lock);
	fsd_rem_thread = NULL;
	mutex_exit(&fsd_rem_thread_lock);

	return (0);
}

static void
fsd_mount_callback(vfs_t *vfsp, void *arg)
{
	_NOTE(ARGUNUSED(arg));

	int error = 0;

	mutex_enter(&fsd_lock);
	if (fsd_omni_param != NULL)
		error = fsd_disturber_install(vfsp, fsd_omni_param);
	mutex_exit(&fsd_lock);

	if (error != 0) {
		refstr_t *mntref;

		mntref = vfs_getmntpoint(vfsp);
		(void) cmn_err(CE_NOTE, "Installing disturber for %s failed.\n",
		    refstr_value(mntref));
		refstr_rele(mntref);
	}
}

/*
 * Although, we might delete the fsd_free_callback(), it would make the whole
 * proces less clear. There's a time window between firing free callbacks and
 * freeing the vfs_t in fsd_disturber_remove() could be called. fsh can
 * deal with invalid handles (until there is no collision), but we'd like to
 * have a nice assertion instead.
 */
static void
fsd_free_callback(vfs_t *vfsp, void *arg)
{
	_NOTE(ARGUNUSED(arg));

	fsd_int_t *fsdi;

	mutex_enter(&fsd_lock);
	for (fsdi = list_head(&fsd_list); fsdi != NULL;
	    fsdi = list_next(&fsd_list, fsdi)) {
		if (fsdi->fsdi_vfsp == vfsp) {
			if (fsdi->fsdi_doomed)
				continue;

			fsdi->fsdi_doomed = 1;
			/*
			 * We make such assertion, because fsd_lock is held
			 * and that means that neither fsd_disturber_remove()
			 * nor fsd_remove_cb() has removed this hook in
			 * different thread.
			 */
			mutex_enter(&fsd_rem_thread_lock);
			fsd_rem_thread = curthread;
			mutex_exit(&fsd_rem_thread_lock);

			ASSERT(fsh_hook_remove(fsdi->fsdi_handle) == 0);

			mutex_enter(&fsd_rem_thread_lock);
			fsd_rem_thread = NULL;
			mutex_exit(&fsd_rem_thread_lock);

			/*
			 * Since there is at most one hook installed by fsd,
			 * we break.
			 */
			break;
		}
	}
	/*
	 * We can't write ASSERT(fsdi != NULL) because it is possible that
	 * there was a concurrent call to fsd_disturber_remove() or
	 * fsd_detach().
	 */
	mutex_exit(&fsd_lock);
}

static void
fsd_enable()
{
	mutex_enter(&fsd_lock);
	fsd_enabled = 1;
	mutex_exit(&fsd_lock);
}

static void
fsd_disable()
{
	mutex_enter(&fsd_lock);
	fsd_enabled = 0;
	mutex_exit(&fsd_lock);
}


/* Entry points */
static int
fsd_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	minor_t instance;
	fsh_callback_t cb = { 0 };

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (fsd_devi != NULL)
		return (DDI_FAILURE);

	instance = ddi_get_instance(dip);
	if (ddi_create_minor_node(dip, "fsd", S_IFCHR, instance,
	    DDI_PSEUDO, 0) == DDI_FAILURE)
		return (DDI_FAILURE);
	fsd_devi = dip;
	ddi_report_dev(fsd_devi);

	list_create(&fsd_list, sizeof (fsd_int_t),
	    offsetof(fsd_int_t, fsdi_node));

	fsd_rand_seed = gethrtime();

	mutex_init(&fsd_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&fsd_rem_thread_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&fsd_cv_empty, NULL, CV_DRIVER, NULL);

	cb.fshc_mount = fsd_mount_callback;
	cb.fshc_free = fsd_free_callback;
	cb.fshc_arg = fsd_omni_param;
	fsd_cb_handle = fsh_callback_install(&cb);
	if (fsd_cb_handle == -1) {
		/* Cleanup */
		list_destroy(&fsd_list);
		cv_destroy(&fsd_cv_empty);
		mutex_destroy(&fsd_rem_thread_lock);
		mutex_destroy(&fsd_lock);
		ddi_remove_minor_node(fsd_devi, NULL);
		fsd_devi = NULL;
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * If fsd_enable() was called and there was no subsequent fsd_disable() call,
 * detach will fail.
 */
static int
fsd_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	fsd_int_t *fsdi;

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ASSERT(dip == fsd_devi);

	/*
	 * No need to hold fsd_lock here. Since only the hooks and callbacks
	 * might be running at this point.
	 */
	if (fsd_enabled)
		return (DDI_FAILURE);

	ddi_remove_minor_node(dip, NULL);
	fsd_devi = NULL;

	/*
	 * 1. Remove the hooks.
	 * 2. Remove the callbacks.
	 *
	 * This order has to be preserved, because of the fact that
	 * fsd_free_callback() is the last stop before a vfs_t is destroyed.
	 * Without it, this might happen:
	 * 		vfs_free()			fsd_detach()
	 * 1.	Handle for the hook is
	 * 	invalidated.
	 * 2.	Fired fsd_remove_cb().
	 * 3.	fsd_remove_cb() hasn't yet    fsd_lock is acquired.
	 * 	acquired the fsd_lock.
	 * 4	Waiting for fsd_lock. That    ASSERT(fsh_hook_remove(..) == 0);
	 * 	means that the hook hasn't    failed, because the handle is
	 * 	been removed from fsd_hooks   already invalid.
	 * 	fsd_hooks yet.
	 *
	 * The ASSERT() here is nice and without a good reason, we don't want
	 * to get rid of it.
	 */
	mutex_enter(&fsd_lock);
	/*
	 * After we set fsd_detaching to 1, hook remove callback (fsd_remove_cb)
	 * won't try to remove entries from fsd_list.
	 */
	fsd_detaching = 1;
	while ((fsdi = list_remove_head(&fsd_list)) != NULL) {
		if (fsdi->fsdi_doomed == 0) {
			fsdi->fsdi_doomed = 1;

			mutex_enter(&fsd_rem_thread_lock);
			fsd_rem_thread = curthread;
			mutex_exit(&fsd_rem_thread_lock);

			/*
			 * fsd_lock is held, so no other thread could have
			 * removed this hook.
			 */
			ASSERT(fsh_hook_remove(fsdi->fsdi_handle) == 0);

			mutex_enter(&fsd_rem_thread_lock);
			fsd_rem_thread = NULL;
			mutex_exit(&fsd_rem_thread_lock);
		}
	}

	while (fsd_list_count > 0)
		cv_wait(&fsd_cv_empty, &fsd_lock);
	mutex_exit(&fsd_lock);
	cv_destroy(&fsd_cv_empty);

	ASSERT(fsh_callback_remove(fsd_cb_handle) == 0);
	if (fsd_omni_param != NULL) {
		kmem_free(fsd_omni_param, sizeof (*fsd_omni_param));
		fsd_omni_param = NULL;
	}

	/* After removing the callback and hooks, it is safe to remove these */
	list_destroy(&fsd_list);
	mutex_destroy(&fsd_rem_thread_lock);
	mutex_destroy(&fsd_lock);

	return (DDI_SUCCESS);
}

static int
fsd_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **resultp)
{
	_NOTE(ARGUNUSED(dip));

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*resultp = fsd_devi;
		return (DDI_SUCCESS);
	case DDI_INFO_DEVT2INSTANCE:
		*resultp = (void *)(uintptr_t)getminor((dev_t)arg);
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

static int
fsd_open(dev_t *devp, int flag, int otyp, cred_t *credp)
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
fsd_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	_NOTE(ARGUNUSED(dev));
	_NOTE(ARGUNUSED(flag));
	_NOTE(ARGUNUSED(otyp));
	_NOTE(ARGUNUSED(credp));

	return (0);
}


/* ioctl(9E) and it's support functions */
static int
fsd_check_param(fsd_t *fsd)
{
	if (fsd->read_less_chance > 100 ||
	    fsd->read_less_r[0] > fsd->read_less_r[1])
		return (EINVAL);
	return (0);
}

static int
fsd_ioctl_disturb(fsd_ioc_t *ioc, int mode, int *rvalp)
{
	file_t *file;
	fsd_dis_t dis;
	int rv;

	if (ddi_copyin(&ioc->fsdioc_dis, &dis, sizeof (dis), mode))
		return (EFAULT);

	if ((rv = fsd_check_param(&dis.fsdd_param)) != 0) {
		*rvalp = rv;
		return (0);
	}

	if ((file = getf((int)dis.fsdd_mnt)) == NULL) {
		*rvalp = EBADFD;
		return (0);
	}

	mutex_enter(&fsd_lock);
	rv = fsd_disturber_install(file->f_vnode->v_vfsp, &dis.fsdd_param);
	mutex_exit(&fsd_lock);

	releasef((int)dis.fsdd_mnt);

	if (rv != 0)
		*rvalp = EAGAIN;
	else
		*rvalp = 0;

	return (0);
}

static int
fsd_ioctl_get_param(fsd_ioc_t *ioc, int mode, int *rvalp)
{
	file_t *file;
	fsd_int_t *fsdi;
	int error = 0;
	int64_t fd;
	vfs_t *vfsp;

	if (ddi_copyin(&ioc->fsdioc_mnt, &fd, sizeof (fd), mode))
		return (EFAULT);

	if ((file = getf((int)fd)) == NULL) {
		*rvalp = EBADFD;
		return (0);
	}
	vfsp = file->f_vnode->v_vfsp;
	releasef((int)fd);


	mutex_enter(&fsd_lock);

	for (fsdi = list_head(&fsd_list); fsdi != NULL;
	    fsdi = list_next(&fsd_list, fsdi)) {
		if (fsdi->fsdi_vfsp == vfsp)
			break;
	}
	if (fsdi == NULL) {
		*rvalp = ENOENT;
		mutex_exit(&fsd_lock);
		return (0);
	}
	rw_enter(&fsdi->fsdi_lock, RW_READER);
	error = ddi_copyout(&fsdi->fsdi_param, &ioc->fsdioc_param,
	    sizeof (fsdi->fsdi_param), mode);
	rw_exit(&fsdi->fsdi_lock);

	mutex_exit(&fsd_lock);


	if (error) {
		return (EFAULT);
	} else {
		*rvalp = 0;
		return (0);
	}
}

static int
fsd_ioctl_get_info(fsd_ioc_t *ioc, int mode, int *rvalp)
{
	fsd_info_t info;

	mutex_enter(&fsd_lock);
	info.fsdinf_enabled = fsd_enabled;
	info.fsdinf_count = fsd_list_count;
	info.fsdinf_omni_on = fsd_omni_param != NULL;
	if (info.fsdinf_omni_on)
		(void) memcpy(&info.fsdinf_omni_param, fsd_omni_param,
		    sizeof (info.fsdinf_omni_param));
	mutex_exit(&fsd_lock);

	if (ddi_copyout(&info, &ioc->fsdioc_info, sizeof (info), mode))
		return (EFAULT);

	*rvalp = 0;
	return (0);
}

static int
fsd_ioctl_get_list(fsd_ioc_t *ioc, int mode, int *rvalp)
{
	fsd_int_t *fsdi;
	fsd_fs_t *fsdfs_list;
	int i;
	int ret = 0;
	int64_t ioc_list_count;

	*rvalp = 0;

	/* Get data */
	if (ddi_copyin(&ioc->fsdioc_list.count, &ioc_list_count,
	    sizeof (ioc_list_count), mode))
		return (EFAULT);
	if (ddi_copyin(&ioc->fsdioc_list.listp, &fsdfs_list,
	    sizeof (fsdfs_list), mode))
		return (EFAULT);


	mutex_enter(&fsd_lock);
	if (ioc_list_count > fsd_list_count)
		ioc_list_count = fsd_list_count;

	/* Copyout */
	if (ddi_copyout(&ioc_list_count, &ioc->fsdioc_list.count,
	    sizeof (ioc_list_count), mode)) {
		ret = EFAULT;
		goto out;
	}
	for (fsdi = list_head(&fsd_list), i = 0;
	    fsdi != NULL && i < ioc_list_count;
	    fsdi = list_next(&fsd_list, fsdi), i++) {
		refstr_t *mntstr = vfs_getmntpoint(fsdi->fsdi_vfsp);
		int len = strlen(refstr_value(mntstr));

		rw_enter(&fsdi->fsdi_lock, RW_READER);
		if (ddi_copyout(refstr_value(mntstr), fsdfs_list[i].fsdf_name,
		    len + 1, mode) ||
		    ddi_copyout(&fsdi->fsdi_param, &fsdfs_list[i].fsdf_param,
		    sizeof (fsdi->fsdi_param), mode)) {
			ret = EFAULT;
		}
		rw_exit(&fsdi->fsdi_lock);
		refstr_rele(mntstr);

		if (ret != 0)
			break;
	}


out:
	mutex_exit(&fsd_lock);
	return (ret);
}

static int
fsd_ioctl_disturb_off(fsd_ioc_t *ioc, int mode, int *rvalp)
{
	file_t *file;
	int64_t fd;

	if (ddi_copyin(&ioc->fsdioc_mnt, &fd, sizeof (fd), mode))
		return (EFAULT);

	if ((file = getf((int)fd)) == NULL) {
		*rvalp = EBADFD;
		return (0);
	}

	mutex_enter(&fsd_lock);
	*rvalp = fsd_disturber_remove(file->f_vnode->v_vfsp);
	releasef((int)fd);
	mutex_exit(&fsd_lock);

	return (0);
}

static int
fsd_ioctl_disturb_omni(fsd_ioc_t *ioc, int mode, int *rvalp)
{
	fsd_t fsd;
	int rv;

	if (ddi_copyin(&ioc->fsdioc_param, &fsd, sizeof (fsd), mode))
		return (EFAULT);

	if ((rv = fsd_check_param(&fsd)) != 0) {
		*rvalp = rv;
		return (0);
	}

	mutex_enter(&fsd_lock);
	if (fsd_omni_param == NULL)
		fsd_omni_param = (fsd_t *)kmem_alloc(sizeof (*fsd_omni_param),
		    KM_SLEEP);
	(void) memcpy(fsd_omni_param, &fsd, sizeof (*fsd_omni_param));
	mutex_exit(&fsd_lock);

	*rvalp = 0;
	return (0);
}


static int
fsd_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
	int *rvalp)
{
	_NOTE(ARGUNUSED(dev));
	_NOTE(ARGUNUSED(credp));

	int enabled;

	mutex_enter(&fsd_lock);
	enabled = fsd_enabled;
	mutex_exit(&fsd_lock);

	if (!enabled && cmd != FSD_ENABLE) {
		*rvalp = ENOTACTIVE;
		return (0);
	}

	switch (cmd) {
	case FSD_ENABLE:
		fsd_enable();
		*rvalp = 0;
		return (0);

	case FSD_DISABLE:
		fsd_disable();
		*rvalp = 0;
		return (0);

	case FSD_GET_PARAM:
		return (fsd_ioctl_get_param((fsd_ioc_t *)arg, mode, rvalp));

	case FSD_DISTURB:
		return (fsd_ioctl_disturb((fsd_ioc_t *)arg, mode, rvalp));

	case FSD_DISTURB_OFF:
		return (fsd_ioctl_disturb_off((fsd_ioc_t *)arg, mode, rvalp));

	case FSD_DISTURB_OMNI:
		return (fsd_ioctl_disturb_omni((fsd_ioc_t *)arg, mode, rvalp));

	case FSD_DISTURB_OMNI_OFF:
		mutex_enter(&fsd_lock);
		if (fsd_omni_param != NULL)
			kmem_free(fsd_omni_param, sizeof (*fsd_omni_param));
		fsd_omni_param = NULL;
		mutex_exit(&fsd_lock);

		*rvalp = 0;
		return (0);

	case FSD_GET_LIST:
		return (fsd_ioctl_get_list((fsd_ioc_t *)arg, mode, rvalp));

	case FSD_GET_INFO:
		return (fsd_ioctl_get_info((fsd_ioc_t *)arg, mode, rvalp));

	default:
		return (ENOTTY);
	}
}

static struct cb_ops cb_ops = {
	fsd_open,	/* open(9E) */
	fsd_close,	/* close(9E) */
	nodev,		/* strategy(9E) */
	nodev,		/* print(9E) */
	nodev,		/* dump(9E) */
	nodev,		/* read(9E) */
	nodev,		/* write(9E) */
	fsd_ioctl,	/* ioctl(9E) */
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
	fsd_getinfo,		/* getinfo */
	nulldev,
	nulldev,		/* probe */
	fsd_attach,		/* attach */
	fsd_detach,		/* detach */
	nodev,
	&cb_ops,		/* cb_ops */
	NULL,			/* bus_ops */
	NULL,			/* power */
	ddi_quiesce_not_needed,	/* quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,	"Filesystem disturber", &dev_ops
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
