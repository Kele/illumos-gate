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

#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/fsh.h>
#include <sys/fsh_impl.h>
#include <sys/id_space.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <sys/list.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/vnode.h>

/*
 * Filesystem hook framework (fsh)
 *
 * 1. Abstract.
 * The main goal of the filesystem hook framework is to provide an easy way to
 * inject client-defined behaviour into vfs/vnode calls. fsh works on
 * vfs_t granularity.
 *
 * Note: In this document, both an fsh_t structure and hooking function for a
 * vnodeop/vfsop is referred to as *hook*.
 *
 *
 * 2. Overview.
 * fsh_t is the main object in the fsh. An fsh_t is a structure containing:
 * 	- pointers to hooking functions
 * 	- an argument to pass (this is shared for all the hooks in a given
 * 	fsh_t)
 *	- a pointer to the *hook remove callback*
 *
 * The information from fsh_t is copied by the fsh and an fsh_handle_t
 * is returned. It should be used for further removing.
 *
 *
 * 3. Usage.
 * It is expected that vfs_t/vnode_t passed to fsh_foo() functions are held by
 * the caller when needed. fsh does no vfs_t/vnode_t locking.
 *
 * fsh_t is a structure filled out by the client. It contains:
 *	- pointers to hooking functions
 *	- the argument passed to the hooks
 *	- the *hook remove callback*
 *
 * If a client does not want to add a hook for function foo(), he should fill
 * corresponding fields with NULLs. For every vfsop/vnodeop there are two
 * fields: pre_foo() and post_foo(). These are the functions called before and
 * after the next hook or underlying vfsop/vnodeop.
 *
 * Pre hooks take:
 *	- arg
 *	- pointer to a field containing void* - it should be filled whenever
 *	the client wants to have some data shared by the pre and post hooks in
 *	the same syscall execution. This is called the *instance data*.
 *	- pointers to the arguments passed to the underlying vfsop/vnodeop
 * Pre hooks return void.
 *
 * Post hooks take:
 *	- value returned by the previous post hook or underlying vfsop/vnodeop
 *	- arg
 *	- pointer to the *instance data*
 *	- arguments passed to the underlying vfsop/vnodeop
 * Post hooks return an int, which should be treated as the vfsop/vnodeop
 * return value.
 * Memory allocated by pre hook must be deallocated by the post hook.
 *
 * Execution path of hooks A, B, C is as follows:
 * foo()
 * 	preA(argA, &instancepA, ...);
 * 	preB(argB, &instancepB, ...);
 * 	preC(argC, &instancepC, ...);
 * 	ret = VOP_FOO();
 * 	ret = postC(ret, argC, instancepC, ...);
 * 	ret = postB(ret, argB, instancepB, ...);
 *	ret = postC(ret, argA, instancepA, ...);
 *	return (ret);
 *
 * After installation, an fsh_handle_t is returned to the caller.
 *
 * Hook remove callback - it's a function being fired after a hook is removed
 * and no thread is going to execute it anymore. It's safe to destroy all the
 * data associated with this hook inside it.
 *
 * It is guaranteed, that whenever a pre_hook() is called, there will be also
 * post_hook() called within the same syscall.
 *
 * If a hook (HNew) is installed/removed on/from a vfs_t within execution of
 * another hook (HExec) installed on this vfs_t, the syscall that executes
 * HExec won't fire HNew.
 *
 * A client might want to fire callbacks when vfs_ts are being mounted
 * or freed. There's an fsh_callback_t structure provided to install such
 * callbacks along with the API.
 * It is legal to call fsh_hook_{install,remove}() inside a mount callback
 * WITHOUT holding the vfs_t.
 *
 * After vfs_t's free callback returns, all the handles associated with the
 * hooks installed on this vfs_t are invalid and must not be used.
 *
 * 4. API
 * None of the APIs should be called during interrupt context above lock
 * level.
 *
 * a) fsh.h
 * Any of these functions could be called in a hook or a hook remove callback.
 * The only functions that must not be called inside a {mount,free} callback are
 * fsd_callback_{install,remove}. Using them will cause a deadlock.
 *
 *
 * fsh_fs_enable(vfs_t *vfsp)
 * fsh_fs_disable(vfs_t *vfsp)
 * 	Enables/disables fsh for a given vfs_t.
 *
 * fsh_hook_install(vfs_t *vfsp, fsh_t *hooks)
 * 	Installs hooks on vfsp filesystem.
 *	It's important that hooks are executed in LIFO installation order,
 *	which means that if there are hooks A and B installed in this order, B
 *	is going to be executed before A.
 *	It returns a correct handle, or (-1) if hook/callback limit exceeded.
 *	The handle is valid until a free callback returns or an explicit call
 *	to fsh_hook_remove().
 *
 * fsh_hook_remove(fsh_handle_t handle)
 * 	Removes a hook and invalidates the handle.
 *	It is guaranteed that after this funcion returns, calls to
 *	vnodeops/vfsops won't go through this hook, although there might be
 *	some threads still executing this hook. When hook remove callback is
 *	fired, it is guaranteed that the hook won't be executed anymore. It is
 *	safe to remove all the internal data associated with this hook inside
 *	the hook remove callback. The hook remove callback could be called
 *	inside fsh_hook_remove().
 *
 *
 * fsh_callback_install(fsh_callback_t *callback)
 * fsh_callback_remove(fsh_callback_handle_t handle)
 * 	Installs/removes callbacks for vfs_t mount/free. The mount callback
 * 	is executed right before domount() returns. The free callback is
 * 	called right before VFS_FREEVFS() is called.
 *	The fsh_callback_install() returns a correct handle, or (-1) if
 *	hook/callback limit exceeded.
 *
 *
 * b) fsh_impl.h (for vfs.c and vnode.c only)
 * fsh_init()
 * 	This call has to be done in vfsinit(). It initialises the fsh. It
 * 	is absolutely necessary that this call is made before any other fsh
 * 	operation.
 *
 * fsh_exec_mount_callbacks(vfs_t *vfsp)
 * fsh_exec_free_callbacks(vfs_t *vfsp)
 * 	Used to execute all fsh callbacks for {mount,free} of a vfs_t.
 *
 * fsh_fsrec_destroy(struct fsh_fsrecord *fsrecp)
 * 	Destroys an fsh_fsrecord structure. All the hooks installed on this
 * 	vfs_t are then destroyed. free callback is called before this function.
 *
 * fsh_foo(ARGUMENTS)
 * 	Function used to execute the hook chain for a given syscall.
 *
 *
 * 5. Internals.
 * fsh_int_t is an internal hook structure. It is reference counted.
 * fshi_hold() and fshi_rele() should be used whenever needed.
 * fsh_int_t entries are elements of both fsh_map (global) and fshfsr_list
 * (local to vfs_t). All entries are unique and are identified by fshi_handle.
 *
 * fsh_int_t properties:
 *	- fsh_hook_install() sets the ref. counter to 1 and adds it to both
 *	fsh_map and fshfsr_list
 *	- fsh_hook_remove() decreases the ref. counter by 1, removes the hook
 *	from fsh_map and marks the hook as *doomed*
 *	- if fsh_int_t is on the fshfsr_list, it's alive and there is a thread
 *	executing it
 *	- if fsh_int_t is marked as *doomed*, the reference counter is not
 *	be increased and thus no thread can acquire this fsh_int_t
 *	- ref. counter can drop to 0 only after an fsh_hook_remove() call; this
 *	also means that the fsh_int_t is *doomed* and isn't a part of fsh_map
 *	- fsh_int_t could be also destroyed without fsh_hook_remove() call,
 *	that happens only inside fsh_fsrec_destroy() where it is guaranteed
 *	that there is no thread executing the hook
 *
 *
 * fsh_fsrecord_t is a structure which lives inside a vfs_t.
 * fsh_fsrecord_t contains:
 *	- an rw-lock that protects the structure
 *	- a list of hooks installed on this vfs_t
 * 	- a flag which tells whether fsh is enabled on this vfs_t
 *
 *
 * fsh_fsrec_prepare rule:
 * Every function that needs vfsp->vfs_fshrecord has to call
 * fsh_fsrec_prepare() first. If and only if the call is made, it is safe to
 * use vfsp->vfs_fshrecord.
 *
 * Unfortunately, because of unexpected behaviour of some filesystems (no use
 * of vfs_alloc()/vfs_init()) there's no good place to initialise the
 * fsh_fshrecord_t structure. The approach being used here is to check if it's
 * initialised in every call. Because of the fact that no lock could be used
 * here (the same problem with initialisation), a spinlock is used.  This is
 * explained in more detail in a comment before fsh_fsrec_prepare(). After
 * calling fsh_preapre_fsrec() it's completely safe to keep the vfs_fshrecord
 * pointer locally, because it won't be changed until vfs_free() is called.
 *
 * Exceptions from this rule:
 * - vfs_free() - it is expected that no other fsh calls would be made for the
 * vfs_t that's being freed. That's why vfs_fshrecord could be only NULL or a
 * valid pointer and could not be concurrently accessed.
 * - fshi_rele() - fsh_hook_install() comes before first fshi_rele() call;
 * the fsh_fsrecord_t has been initialised there
 *
 *
 * When there are no fsh functions (that use a particular fsh_fsrecord_t)
 * executing, the vfs_fshrecord pointer won't be equal to fsh_res_ptr. It
 * would be NULL or a pointer to an initialised fsh_fsrecord_t.
 *
 * It is required and sufficient to check if fsh_fsrecord_t is not NULL before
 * passing it to fsh_fsrec_destroy. We don't have to check if it is not equal
 * to fsh_res_ptr, because all the fsh API calls involving this vfs_t should
 * end before vfs_free() is called (outside the fsh, fsh_fsrecord is never
 * equal to fsh_res_ptr). That is guaranteed by the explicit requirement that
 * the caller of fsh API holds the vfs_t when needed. fsh_hook_remove() must not
 * be called either, because the handles are invalidated after free callback has
 * fired.
 *
 *
 * Callbacks:
 * Mount callbacks are executed by a call to fsh_exec_mount_callbacks() right
 * before returning from domount()@vfs.c.
 *
 * Free callbacks are executed by a call to fsh_exec_free_callbacks() right
 * before calling VFS_FREEVFS(), after vfs_t's reference count drops to 0.
 *
 *
 * 6. Locking
 * a) public
 * fsh does no vfs_t nor vnode_t locking. It is expected that whenever it is
 * needed, the client does that.
 *
 * No locks are held across hooks or hook remove callbacks execution. It is
 * safe to use fsh API inside hooks and hook remove callbacks.
 *
 * fsh_cb_lock is held across {mount,free} callbacks. Calling
 * fsh_callback_{install,remove} inside of a callback will cause a deadlock.
 *
 * b) internals
 * Locking diagram:
 *
 *     fsh_hook_remove()          fsh_hook_install()   fsh_fsrec_destroy()
 *           |                            |                |
 *           |                            |                |
 *           +------------------+         |   +------------+
 *           |                  |         |   |
 *           |                  V         |   |
 *           V               +------------|---|-+
 *      fshi_rele()          |  fsh_lock  |   | |
 *      (sometimes)          +------------|---|-+
 *                                 |      |   |
 *                                 |      +---+-- fshfsr_lock, RW_WRITER -+
 *                                 |                                      |
 *                                 V                                      |
 *               +---------------------------------------+                |
 *               |               fsh_map                 |                |
 *               |                                       |                |
 *          +----|-> vfsp->vfs_fshrecord->fshfsr_list <--|----------------+
 *          |    +------------------------------^--------+
 *          |                                   |
 *          |                                   |
 * fshfsr_lock, RW_READER              fshfsr_lock, RW_WRITER
 *          |                                   |
 *          |                                   |
 *   fsh_read(),                            fshi_rele()
 *   fsh_write(),
 *   ...                                Might be called from:
 *                                        fsh_hook_remove()
 *                                        fsh_read(), fsh_write(), ...
 *
 *
 * fsh_lock is a global lock for adminsitrative path (fsh_hook_install,
 * fsh_hook_remove) and fsh_fsrec_destroy() (which is semi-administrative, since
 * it destroys the unremoved hooks). It is used only when fsh_map needs to be
 * locked. The usage of this lock guarantees that the data in fsh_map and
 * fshfsr_lists is consistent.
 *
 * In order to make calling callbacks inside callbacks possible, fsh_cb_owner is
 * set by fsh_exec_{mount,free} callbacks to the thread that owns the
 * fsh_cb_lock.  It's always checked if we are owners of the mutex before
 * entering it.
 *
 */


/* Internals */
typedef struct fsh_int {
	fsh_handle_t	fshi_handle;
	fsh_t		fshi_hooks;
	vfs_t		*fshi_vfsp;

	kmutex_t	fshi_lock;
	uint64_t	fshi_ref;
	uint64_t	fshi_doomed;	/* changed inside fsh_lock */

	/* next node in fshfsr_list */
	list_node_t	fshi_node;

	/* next node in fsh_map */
	list_node_t	fshi_global;
} fsh_int_t;

typedef struct fsh_callback_int {
	fsh_callback_t	fshci_cb;
	fsh_callback_handle_t fshci_handle;
	list_node_t	fshci_node;
} fsh_callback_int_t;


typedef struct fsh_exec {
	fsh_int_t	*fshe_fshi;
	void		*fshe_instance;
	list_node_t	fshe_node;
} fsh_exec_t;


static kmutex_t fsh_lock;

/*
 * fsh_fsrecord_t is the main internal structure. It's content is protected
 * by fshfsr_lock. The fshfsr_list is a list of fsh_int_t hook entries for
 * the vfs_t that contains the fsh_fsrecord_t.
 */
struct fsh_fsrecord {
	krwlock_t	fshfsr_lock;
	int		fshfsr_enabled;
	list_t		fshfsr_list;
};

/*
 * Global list of fsh_int_t. Protected by fsh_lock.
 */
static list_t fsh_map;

/*
 * Global list of fsh_callback_int_t.
 */
static kmutex_t fsh_cb_lock;
static kmutex_t fsh_cb_owner_lock;
static kthread_t *fsh_cb_owner;
static list_t fsh_cblist;

/*
 * A reserved pointer for fsh purposes. It is used because of the method
 * chosen for solving concurrency issues with vfs_fshrecord. The full
 * explanation is in the big theory statement at the beginning of this
 * file and above fsh_fsrec_prepare(). It is initialised in fsh_init().
 */
static void *fsh_res_ptr;

static fsh_fsrecord_t *fsh_fsrec_create();

int fsh_limit = INT_MAX;
static id_space_t *fsh_idspace;

/*
 * fsh_fsrec_prepare()
 *
 * Important note:
 * Before using this function, fsh_init() MUST be called. We do that in
 * vfsinit()@vfs.c.
 *
 * One would ask, why isn't the vfsp->vfs_fshrecord initialised when the
 * vfs_t is created. Unfortunately, some filesystems (e.g. fifofs) do not
 * call vfs_init() or even vfs_alloc(), It's possible that some unbundled
 * filesystems could do the same thing. That's why this solution is
 * introduced. It should be called before any code that needs access to
 * vfs_fshrecord.
 *
 * Locking:
 * There are no locks here, because there's no good place to initialise
 * the lock. Concurrency issues are solved by using atomic instructions
 * and a spinlock, which is spinning only once for a given vfs_t. Because
 * of that, the usage of the spinlock isn't bad at all.
 *
 * How it works:
 * a) if vfsp->vfs_fshrecord equals NULL, atomic_cas_ptr() changes it to
 *	fsh_res_ptr. That's a signal for other threads, that the structure
 *	is being initialised.
 * b) if vfsp->vfs_fshrecord equals fsh_res_ptr, that means we have to wait,
 *	because vfs_fshrecord is being initialised by another call.
 * c) other cases:
 *	vfs_fshrecord is already initialised, so we can use it. It won't change
 *	until vfs_free() is called. It can't happen when someone is holding
 *	the vfs_t, which is expected from the caller of fsh API.
 */
static void
fsh_fsrec_prepare(vfs_t *vfsp)
{
	fsh_fsrecord_t *fsrec;

	while ((fsrec = atomic_cas_ptr(&vfsp->vfs_fshrecord, NULL,
	    fsh_res_ptr)) == fsh_res_ptr)
		;

	if (fsrec == NULL)
		atomic_swap_ptr(&vfsp->vfs_fshrecord, fsh_fsrec_create());
}

/*
 * API for enabling/disabling fsh per vfs_t.
 *
 * A newly created vfs_t has fsh enabled by default. If one would want to change
 * this behaviour, mount callbacks could be used.
 *
 * The caller is expected to hold the vfs_t.
 *
 * These functions must NOT be called in a hook.
 */
void
fsh_fs_enable(vfs_t *vfsp)
{
	fsh_fsrec_prepare(vfsp);

	rw_enter(&vfsp->vfs_fshrecord->fshfsr_lock, RW_WRITER);
	vfsp->vfs_fshrecord->fshfsr_enabled = 1;
	rw_exit(&vfsp->vfs_fshrecord->fshfsr_lock);
}

void
fsh_fs_disable(vfs_t *vfsp)
{
	fsh_fsrec_prepare(vfsp);

	rw_enter(&vfsp->vfs_fshrecord->fshfsr_lock, RW_WRITER);
	vfsp->vfs_fshrecord->fshfsr_enabled = 0;
	rw_exit(&vfsp->vfs_fshrecord->fshfsr_lock);
}

/*
 * API used for installing hooks. fsh_handle_t is returned for further
 * actions (currently just removing) on this set of hooks.
 *
 * It's important that the hooks are executed in LIFO installation order (they
 * are added to the head of the hook list).
 *
 * The caller is expected to hold the vfs_t.
 *
 * Returns (-1) if hook/callback limit exceeded, handle otherwise.
 */
fsh_handle_t
fsh_hook_install(vfs_t *vfsp, fsh_t *hooks)
{
	fsh_handle_t	handle;
	fsh_int_t	*fshi;

	fsh_fsrec_prepare(vfsp);

	if ((handle = id_alloc(fsh_idspace)) == -1)
		return (-1);

	fshi = kmem_alloc(sizeof (*fshi), KM_SLEEP);
	mutex_init(&fshi->fshi_lock, NULL, MUTEX_DRIVER, NULL);
	(void) memcpy(&fshi->fshi_hooks, hooks, sizeof (fshi->fshi_hooks));
	fshi->fshi_handle = handle;
	fshi->fshi_doomed = 0;
	fshi->fshi_ref = 1;
	fshi->fshi_vfsp = vfsp;

	mutex_enter(&fsh_lock);
	rw_enter(&vfsp->vfs_fshrecord->fshfsr_lock, RW_WRITER);
	list_insert_head(&vfsp->vfs_fshrecord->fshfsr_list, fshi);
	rw_exit(&vfsp->vfs_fshrecord->fshfsr_lock);

	list_insert_head(&fsh_map, fshi);
	mutex_exit(&fsh_lock);

	return (handle);
}

static int
fshi_hold(fsh_int_t *fshi)
{
	int can_hold;

	mutex_enter(&fshi->fshi_lock);
	if (fshi->fshi_doomed == 1) {
		can_hold = 0;
	} else {
		fshi->fshi_ref++;
		can_hold = 1;
	}
	mutex_exit(&fshi->fshi_lock);

	return (can_hold);
}

/*
 * This function must not be called while fshfsr_lock is held. Doing so could
 * cause a deadlock.
 */
static void
fshi_rele(fsh_int_t *fshi)
{
	int destroy;

	mutex_enter(&fshi->fshi_lock);
	ASSERT(fshi->fshi_ref > 0);
	fshi->fshi_ref--;
	if (fshi->fshi_ref == 0) {
		ASSERT(fshi->fshi_doomed == 1);
		destroy = 1;
	} else {
		destroy = 0;
	}
	mutex_exit(&fshi->fshi_lock);

	if (destroy) {
		/*
		 * At this point, we are sure that fsh_hook_remove() has been
		 * called, that's why we don't remove the fshi from fsh_map.
		 * fsh_hook_remove() did that already.
		 * There is also no need to call fsh_fsrec_prepare() here.
		 */
		fsh_fsrecord_t *fsrecp;

		/*
		 * We don't have to call fsh_fsrec_prepare() here.
		 * fsh_fsrecord_t is already initialised, because we've found a
		 * mapping for the given handle.
		 */
		fsrecp = fshi->fshi_vfsp->vfs_fshrecord;
		ASSERT(fsrecp != NULL);
		ASSERT(fsrecp != fsh_res_ptr);

		rw_enter(&fsrecp->fshfsr_lock, RW_WRITER);
		list_remove(&fsrecp->fshfsr_list, fshi);
		rw_exit(&fsrecp->fshfsr_lock);

		if (fshi->fshi_hooks.remove_cb != NULL)
			(*fshi->fshi_hooks.remove_cb)(
			    fshi->fshi_hooks.arg, fshi->fshi_handle);

		id_free(fsh_idspace, fshi->fshi_handle);
		mutex_destroy(&fshi->fshi_lock);
		kmem_free(fshi, sizeof (*fshi));
	}
}

/*
 * Used for removing a hook set.
 *
 * fsh_hook_remove() invalidates the given handle.
 *
 * It is guaranteed, that after successful return from fsh_hook_remove(),
 * calls to vnodeops/vfsops, on the vfs_t on which the hook is installed, won't
 * go through this hook.
 *
 * There is no guarantee that after fsh_hook_remove() returns, the hook
 * associated with the handle won't be executing. Instead, it is guaranteed that
 * when remove_cb() is called, the hook finished it's execution in all threads.
 * It is safe to destroy all internal data associated with this hook inside
 * remove_cb().
 *
 * It is possible that remove_cb() would be called before fsh_hook_remove()
 * returns.
 *
 * Returns (-1) if hook wasn't found, 0 otherwise.
 */
int
fsh_hook_remove(fsh_handle_t handle)
{
	fsh_int_t	*fshi;

	mutex_enter(&fsh_lock);
	for (fshi = list_head(&fsh_map); fshi != NULL;
	    fshi = list_next(&fsh_map, fshi)) {
		if (fshi->fshi_handle == handle) {
			list_remove(&fsh_map, fshi);
			break;
		}
	}

	if (fshi == NULL)
		return (-1);

	mutex_enter(&fshi->fshi_lock);
	ASSERT(fshi->fshi_doomed == 0);
	fshi->fshi_doomed = 1;
	mutex_exit(&fshi->fshi_lock);
	mutex_exit(&fsh_lock);

	fshi_rele(fshi);

	return (0);
}

/*
 * API for installing global mount/free callbacks.
 *
 * fsh_callback_t fields:
 * fshc_arg - argument passed to the callbacks
 * fshc_free - callback fired before VFS_FREEVFS() is called, after vfs_count
 *	drops to 0
 * fshc_mount - callback fired right before returning from domount()
 * The first argument of these callbacks is the vfs_t that is mounted/freed.
 * The second one is the fshc_arg.
 *
 * fsh_callback_handle_t is filled out by this function.
 *
 * Returns (-1) if hook/callback limit exceeded.
 *
 * Calling this function in a {mount,free} callback will cause a deadlock.
 */
fsh_callback_handle_t
fsh_callback_install(fsh_callback_t *callback)
{
	fsh_callback_int_t *fshci;
	fsh_callback_handle_t handle;

	if ((handle = id_alloc(fsh_idspace)) == -1)
		return (-1);

	fshci = (fsh_callback_int_t *)kmem_alloc(sizeof (*fshci), KM_SLEEP);
	(void) memcpy(&fshci->fshci_cb, callback, sizeof (fshci->fshci_cb));
	fshci->fshci_handle = handle;

	mutex_enter(&fsh_cb_lock);
	list_insert_head(&fsh_cblist, fshci);
	mutex_exit(&fsh_cb_lock);

	return (handle);
}

/*
 * API for removing global mount/free callbacks.
 *
 * Returns (-1) if callback wasn't found, 0 otherwise.
 *
 * Calling this function in a {mount,free} callback will cause a deadlock.
 */
int
fsh_callback_remove(fsh_callback_handle_t handle)
{
	fsh_callback_int_t *fshci;

	mutex_enter(&fsh_cb_lock);

	for (fshci = list_head(&fsh_cblist); fshci != NULL;
	    fshci = list_next(&fsh_cblist, fshci)) {
		if (fshci->fshci_handle == handle) {
			list_remove(&fsh_cblist, fshci);
			break;
		}
	}

	mutex_exit(&fsh_cb_lock);

	if (fshci == NULL)
		return (-1);

	kmem_free(fshci, sizeof (*fshci));
	id_free(fsh_idspace, handle);

	return (0);
}

/*
 * This function is executed right before returning from domount()@vfs.c.
 * We are sure that it's called only after fsh_init().
 * It executes all the mount callbacks installed in the fsh.
 *
 * Since fsh_exec_mount_callbacks() is called only inside domount(), it is legal
 * to call fsh_hook_{install,remove}() inside a mount callback WITHOUT holding
 * this vfs_t. This guarantee should be preserved, because it's in the "Usage"
 * section in the big theory statement at the top of this file.
 */
void
fsh_exec_mount_callbacks(vfs_t *vfsp)
{
	fsh_callback_int_t *fshci;
	fsh_callback_t *cb;
	int fsh_context;

	mutex_enter(&fsh_cb_owner_lock);
	fsh_context = fsh_cb_owner == curthread;
	mutex_exit(&fsh_cb_owner_lock);

	if (!fsh_context) {
		mutex_enter(&fsh_cb_lock);
		mutex_enter(&fsh_cb_owner_lock);
		fsh_cb_owner = curthread;
		mutex_exit(&fsh_cb_owner_lock);
	}

	ASSERT(MUTEX_HELD(&fsh_cb_lock));

	for (fshci = list_head(&fsh_cblist); fshci != NULL;
	    fshci = list_next(&fsh_cblist, fshci)) {
		cb = &fshci->fshci_cb;
		if (cb->fshc_mount != NULL)
			(*(cb->fshc_mount))(vfsp, cb->fshc_arg);
	}

	if (!fsh_context) {
		mutex_enter(&fsh_cb_owner_lock);
		fsh_cb_owner = NULL;
		mutex_exit(&fsh_cb_owner_lock);
		mutex_exit(&fsh_cb_lock);
	}
}

/*
 * This function is executed right before VFS_FREEVFS() is called in
 * vfs_rele()@vfs.c. We are sure that it's called only after fsh_init().
 * It executes all the free callbacks installed in the fsh.
 *
 * free() callback is the point after the handles associated with the hooks
 * installed on this vfs_t become invalid
 */
void
fsh_exec_free_callbacks(vfs_t *vfsp)
{
	fsh_callback_int_t *fshci;
	fsh_callback_t *cb;
	int fsh_context;

	mutex_enter(&fsh_cb_owner_lock);
	fsh_context = fsh_cb_owner == curthread;
	mutex_exit(&fsh_cb_owner_lock);

	if (!fsh_context) {
		mutex_enter(&fsh_cb_lock);
		mutex_enter(&fsh_cb_owner_lock);
		fsh_cb_owner = curthread;
		mutex_exit(&fsh_cb_owner_lock);
	}

	ASSERT(MUTEX_HELD(&fsh_cb_lock));

	for (fshci = list_head(&fsh_cblist); fshci != NULL;
	    fshci = list_next(&fsh_cblist, fshci)) {
		cb = &fshci->fshci_cb;
		if (cb->fshc_free != NULL)
			(*(cb->fshc_free))(vfsp, cb->fshc_arg);
	}

	if (!fsh_context) {
		mutex_enter(&fsh_cb_owner_lock);
		fsh_cb_owner = NULL;
		mutex_exit(&fsh_cb_owner_lock);
		mutex_exit(&fsh_cb_lock);
	}
}

/*
 * API for vnode.c/vfs.c to start executing the fsh for a given operation.
 *
 * fsh_xxx() tries to find the first non-NULL xxx hook on the fshfsr_list. If it
 * does, it executes it. If not, underlying vnodeop/vfsop is called.
 *
 * These interfaces are using fsh_res_ptr (in fsh_fsrec_prepare()), so it's
 * absolutely necessary to call fsh_init() before using them. That's done in
 * vfsinit().
 *
 * While these functions are executing, it's expected that necessary vfs_t's
 * are held so that vfs_free() isn't called. vfs_free() expects that noone
 * accesses vfs_fshrecord of a given vfs_t.
 * It's also the caller's responsibility to keep vnode_t passed to fsh_foo()
 * alive and valid.
 * All these expectations are met because these functions are used only in
 * correspondng {fop,fsop}_foo() functions.
 */
int
fsh_read(vnode_t *vp, uio_t *uiop, int ioflag, cred_t *cr,
	caller_context_t *ct)
{
	int ret;
	fsh_fsrecord_t *fsrecp;
	fsh_int_t *fshi;
	fsh_exec_t *fshe;
	list_t exec_list;

	fsh_fsrec_prepare(vp->v_vfsp);
	fsrecp = vp->v_vfsp->vfs_fshrecord;

	rw_enter(&fsrecp->fshfsr_lock, RW_READER);
	if (!(fsrecp->fshfsr_enabled)) {
		rw_exit(&fsrecp->fshfsr_lock);
		return ((*vp->v_op->vop_read)(vp, uiop, ioflag, cr, ct));
	}

	list_create(&exec_list, sizeof (fsh_exec_t),
	    offsetof(fsh_exec_t, fshe_node));

	for (fshi = list_head(&fsrecp->fshfsr_list); fshi != NULL;
	    fshi = list_next(&fsrecp->fshfsr_list, fshi)) {
		if (fshi->fshi_hooks.pre_read != NULL ||
		    fshi->fshi_hooks.post_read != NULL) {
			if (fshi_hold(fshi)) {
				fshe = kmem_alloc(sizeof (*fshe), KM_SLEEP);
				fshe->fshe_fshi = fshi;
				list_insert_tail(&exec_list, fshe);
			}
		}
	}
	rw_exit(&fsrecp->fshfsr_lock);

	/* Execute pre hooks */
	for (fshe = list_head(&exec_list); fshe != NULL;
	    fshe = list_next(&exec_list, fshe)) {
		if (fshe->fshe_fshi->fshi_hooks.pre_read != NULL)
			(*fshe->fshe_fshi->fshi_hooks.pre_read)(
			    fshe->fshe_fshi->fshi_hooks.arg,
			    &fshe->fshe_instance,
			    &vp, &uiop, &ioflag, &cr, &ct);
	}

	ret = (*vp->v_op->vop_read)(vp, uiop, ioflag, cr, ct);

	/* Execute post hooks */
	while ((fshe = list_remove_tail(&exec_list)) != NULL) {
		if (fshe->fshe_fshi->fshi_hooks.post_read != NULL)
			ret = (*fshe->fshe_fshi->fshi_hooks.post_read)(
			    ret, fshe->fshe_fshi->fshi_hooks.arg,
			    fshe->fshe_instance,
			    vp, uiop, ioflag, cr, ct);
		fshi_rele(fshe->fshe_fshi);
		kmem_free(fshe, sizeof (*fshe));
	}
	list_destroy(&exec_list);

	return (ret);
}

int
fsh_write(vnode_t *vp, uio_t *uiop, int ioflag, cred_t *cr,
	caller_context_t *ct)
{
	int ret;
	fsh_fsrecord_t *fsrecp;
	fsh_int_t *fshi;
	fsh_exec_t *fshe;
	list_t exec_list;

	fsh_fsrec_prepare(vp->v_vfsp);
	fsrecp = vp->v_vfsp->vfs_fshrecord;

	rw_enter(&fsrecp->fshfsr_lock, RW_READER);
	if (!(fsrecp->fshfsr_enabled)) {
		rw_exit(&fsrecp->fshfsr_lock);
		return ((*vp->v_op->vop_write)(vp, uiop, ioflag, cr, ct));
	}

	list_create(&exec_list, sizeof (fsh_exec_t),
	    offsetof(fsh_exec_t, fshe_node));

	for (fshi = list_head(&fsrecp->fshfsr_list); fshi != NULL;
	    fshi = list_next(&fsrecp->fshfsr_list, fshi)) {
		if (fshi->fshi_hooks.pre_write != NULL ||
		    fshi->fshi_hooks.post_write != NULL) {
			if (fshi_hold(fshi)) {
				fshe = kmem_alloc(sizeof (*fshe), KM_SLEEP);
				fshe->fshe_fshi = fshi;
				list_insert_tail(&exec_list, fshe);
			}
		}
	}
	rw_exit(&fsrecp->fshfsr_lock);

	/* Execute pre hooks */
	for (fshe = list_head(&exec_list); fshe != NULL;
	    fshe = list_next(&exec_list, fshe)) {
		if (fshe->fshe_fshi->fshi_hooks.pre_write != NULL)
			(*fshe->fshe_fshi->fshi_hooks.pre_write)(
			    fshe->fshe_fshi->fshi_hooks.arg,
			    &fshe->fshe_instance,
			    &vp, &uiop, &ioflag, &cr, &ct);
	}

	ret = (*vp->v_op->vop_write)(vp, uiop, ioflag, cr, ct);

	/* Execute post hooks */
	while ((fshe = list_remove_tail(&exec_list)) != NULL) {
		if (fshe->fshe_fshi->fshi_hooks.post_write != NULL)
			ret = (*fshe->fshe_fshi->fshi_hooks.post_write)(
			    ret, fshe->fshe_fshi->fshi_hooks.arg,
			    fshe->fshe_instance,
			    vp, uiop, ioflag, cr, ct);
		fshi_rele(fshe->fshe_fshi);
		kmem_free(fshe, sizeof (*fshe));
	}
	list_destroy(&exec_list);

	return (ret);
}

int
fsh_mount(vfs_t *vfsp, vnode_t *mvp, struct mounta *uap, cred_t *cr)
{
	int ret;
	fsh_fsrecord_t *fsrecp;
	fsh_int_t *fshi;
	fsh_exec_t *fshe;
	list_t exec_list;

	fsh_fsrec_prepare(vfsp);
	fsrecp = vfsp->vfs_fshrecord;

	rw_enter(&fsrecp->fshfsr_lock, RW_READER);
	if (!(fsrecp->fshfsr_enabled)) {
		rw_exit(&fsrecp->fshfsr_lock);
		return ((*vfsp->vfs_op->vfs_mount)(vfsp, mvp, uap, cr));
	}

	list_create(&exec_list, sizeof (fsh_exec_t),
	    offsetof(fsh_exec_t, fshe_node));

	for (fshi = list_head(&fsrecp->fshfsr_list); fshi != NULL;
	    fshi = list_next(&fsrecp->fshfsr_list, fshi)) {
		if (fshi->fshi_hooks.pre_mount != NULL ||
		    fshi->fshi_hooks.post_mount != NULL) {
			if (fshi_hold(fshi)) {
				fshe = kmem_alloc(sizeof (*fshe), KM_SLEEP);
				fshe->fshe_fshi = fshi;
				list_insert_tail(&exec_list, fshe);
			}
		}
	}
	rw_exit(&fsrecp->fshfsr_lock);

	/* Execute pre hooks */
	for (fshe = list_head(&exec_list); fshe != NULL;
	    fshe = list_next(&exec_list, fshe)) {
		if (fshe->fshe_fshi->fshi_hooks.pre_mount != NULL)
			(*fshe->fshe_fshi->fshi_hooks.pre_mount)(
			    &fshe->fshe_fshi->fshi_hooks.arg,
			    &fshe->fshe_instance,
			    &vfsp, &mvp, &uap, &cr);
	}

	ret = (*vfsp->vfs_op->vfs_mount)(vfsp, mvp, uap, cr);

	/* Execute post hooks */
	while ((fshe = list_remove_tail(&exec_list)) != NULL) {
		if (fshe->fshe_fshi->fshi_hooks.post_mount != NULL)
			ret = (*fshe->fshe_fshi->fshi_hooks.post_mount)(
			    ret, fshe->fshe_fshi->fshi_hooks.arg,
			    fshe->fshe_instance,
			    vfsp, mvp, uap, cr);
		fshi_rele(fshe->fshe_fshi);
		kmem_free(fshe, sizeof (*fshe));
	}
	list_destroy(&exec_list);

	return (ret);
}

int
fsh_unmount(vfs_t *vfsp, int flag, cred_t *cr)
{
	int ret;
	fsh_fsrecord_t *fsrecp;
	fsh_int_t *fshi;
	fsh_exec_t *fshe;
	list_t exec_list;

	fsh_fsrec_prepare(vfsp);
	fsrecp = vfsp->vfs_fshrecord;

	rw_enter(&fsrecp->fshfsr_lock, RW_READER);
	if (!(fsrecp->fshfsr_enabled)) {
		rw_exit(&fsrecp->fshfsr_lock);
		return ((*vfsp->vfs_op->vfs_unmount)(vfsp, flag, cr));
	}

	list_create(&exec_list, sizeof (fsh_exec_t),
	    offsetof(fsh_exec_t, fshe_node));

	for (fshi = list_head(&fsrecp->fshfsr_list); fshi != NULL;
	    fshi = list_next(&fsrecp->fshfsr_list, fshi)) {
		if (fshi->fshi_hooks.pre_unmount != NULL ||
		    fshi->fshi_hooks.post_unmount != NULL) {
			if (fshi_hold(fshi)) {
				fshe = kmem_alloc(sizeof (*fshe), KM_SLEEP);
				fshe->fshe_fshi = fshi;
				list_insert_tail(&exec_list, fshe);
			}
		}
	}
	rw_exit(&fsrecp->fshfsr_lock);

	/* Execute pre hooks */
	for (fshe = list_head(&exec_list); fshe != NULL;
	    fshe = list_next(&exec_list, fshe)) {
		if (fshe->fshe_fshi->fshi_hooks.pre_unmount != NULL)
			(*fshe->fshe_fshi->fshi_hooks.pre_unmount)(
			    fshe->fshe_fshi->fshi_hooks.arg,
			    &fshe->fshe_instance,
			    &vfsp, &flag, &cr);
	}

	ret = (*vfsp->vfs_op->vfs_unmount)(vfsp, flag, cr);

	/* Execute post hooks */
	while ((fshe = list_remove_tail(&exec_list)) != NULL) {
		if (fshe->fshe_fshi->fshi_hooks.post_unmount != NULL)
			ret = (*fshe->fshe_fshi->fshi_hooks.post_unmount)(
			    ret, fshe->fshe_fshi->fshi_hooks.arg,
			    fshe->fshe_instance,
			    vfsp, flag, cr);
		fshi_rele(fshe->fshe_fshi);
		kmem_free(fshe, sizeof (*fshe));
	}
	list_destroy(&exec_list);

	return (ret);
}

/*
 * This is the funtion used by fsh_fsrec_prepare() to allocate a new
 * fsh_fsrecord. This function is called by the first function which
 * access the vfs_fshrecord and finds out it's NULL.
 */
static fsh_fsrecord_t *
fsh_fsrec_create()
{
	fsh_fsrecord_t *fsrecp;

	fsrecp = (fsh_fsrecord_t *)kmem_zalloc(sizeof (*fsrecp), KM_SLEEP);
	list_create(&fsrecp->fshfsr_list, sizeof (fsh_int_t),
	    offsetof(fsh_int_t, fshi_node));
	rw_init(&fsrecp->fshfsr_lock, NULL, RW_DRIVER, NULL);
	fsrecp->fshfsr_enabled = 1;
	return (fsrecp);
}


/*
 * This call must be used ONLY in vfs_free().
 *
 * It is required and sufficient to check if fsh_fsrecord_t is not NULL before
 * passing it to fsh_fsrec_destroy.
 *
 * All the remaining hooks are being removed here.
 */
void
fsh_fsrec_destroy(struct fsh_fsrecord *volatile fsrecp)
{
	fsh_int_t *fshi;

	VERIFY(fsrecp != NULL);

	_NOTE(CONSTCOND)
	while (1) {
		mutex_enter(&fsh_lock);
		rw_enter(&fsrecp->fshfsr_lock, RW_WRITER);
		fshi = list_remove_head(&fsrecp->fshfsr_list);
		rw_exit(&fsrecp->fshfsr_lock);
		if (fshi == NULL) {
			mutex_exit(&fsh_lock);
			break;
		}
		ASSERT(fshi->fshi_doomed == 0);
		list_remove(&fsh_map, fshi);
		mutex_exit(&fsh_lock);

		if (fshi->fshi_hooks.remove_cb != NULL)
			(*fshi->fshi_hooks.remove_cb)(fshi->fshi_hooks.arg,
			    fshi->fshi_handle);

		id_free(fsh_idspace, fshi->fshi_handle);
		mutex_destroy(&fshi->fshi_lock);
		kmem_free(fshi, sizeof (*fshi));

	}

	list_destroy(&fsrecp->fshfsr_list);
	rw_destroy(&fsrecp->fshfsr_lock);
	kmem_free(fsrecp, sizeof (*fsrecp));
}

/*
 * fsh_init() is called in vfsinit()@vfs.c. This function MUST be called
 * before every other fsh call.
 */
void
fsh_init(void)
{
	mutex_init(&fsh_cb_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&fsh_cb_owner_lock, NULL, MUTEX_DRIVER, NULL);
	list_create(&fsh_cblist, sizeof (fsh_callback_int_t),
	    offsetof(fsh_callback_int_t, fshci_node));

	mutex_init(&fsh_lock, NULL, MUTEX_DRIVER, NULL);

	list_create(&fsh_map, sizeof (fsh_int_t), offsetof(fsh_int_t,
	    fshi_global));

	/* See comment above fsh_fsrec_prepare() */
	fsh_res_ptr = (void *)-1;

	fsh_idspace = id_space_create("fsh", 0, fsh_limit);
}
