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

#include <libfsd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fsd_handle_t handle;

int ret;

int
errout(fsd_handle_t *handle)
{
	(void) fprintf(stderr, "Error: %s: %s\n",
	    fsd_strerr(handle->fsd_errno), strerror(handle->errno));

	return (-1);
}

void
print_fsd(fsd_t *fsd)
{
	(void) printf("\tRead less: %d%% chance with range %d - %d\n",
	    (int)fsd->read_less_chance,
	    (int)fsd->read_less_r[0],
	    (int)fsd->read_less_r[1]);
}

void
info()
{
	fsd_info_t info;

	if (fsd_get_info(&handle, &info) != 0) {
		ret = errout(&handle);
		return;
	}

	(void) printf(
	    "Enabled: %s\n"
	    "Filesystems disturbed: %d\n",
	    (info.fsdinf_enabled ? "yes" : "no"), (int)info.fsdinf_count);

	if (info.fsdinf_omni_on) {
		(void) printf(
		    "Omnipresent disturbing: yes\n"
		    "Omnipresent params:\n");
		print_fsd(&info.fsdinf_omni_param);
	} else {
		(void) printf("Omnipresent disturbing: no\n");
	}
}

void
list()
{
	fsd_info_t info;
	fsd_fs_t *fslistp;
	int i;
	int count;

	if (fsd_get_info(&handle, &info) != 0) {
		ret = errout(&handle);
		return;
	}
	count = info.fsdinf_count;

	fslistp = calloc(info.fsdinf_count, sizeof (fsd_fs_t));
	if (fsd_get_list(&handle, fslistp, &count) != 0) {
		ret = errout(&handle);
	} else {
		for (i = 0; i < count; i++) {
			(void) printf("Mountpoint: %s\n",
			    fslistp[i].fsdf_name);
			print_fsd(&fslistp[i].fsdf_param);
			(void) printf("\n");

		}
	}

	free(fslistp);
}

int lflag;
int dflag;
int eflag;
int gflag;
int iflag;
int mflag;
int oflag;
int xflag;

char *mnt;
fsd_t param;

int
main(int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	int opt;

	while ((opt = getopt(argc, argv, "hedi:lm:gxo")) != -1) {
		switch (opt) {
		case 'h':
			goto usage;
			break;

		case 'e':
			eflag = 1;
			break;

		case 'd':
			dflag = 1;
			break;

		case 'i': {
			char *type;

			iflag = 1;

			type = argv[optind-1];
			if (strcmp(type, "readless") == 0) {
				if (optind + 2 >= argc) {
					(void) fprintf(stderr,
					    "Error: \"readless\" requires "
					    " three parameters.\n");
					ret = -1;
					goto end;
				}

				param.read_less_chance = atoi(argv[optind]);
				param.read_less_r[0] = atoi(argv[optind+1]);
				param.read_less_r[1] = atoi(argv[optind+2]);

				optind = optind + 3;

			} else {
				(void) fprintf(stderr,
				    "Error: Invalid disturber type.\n");
				ret = -1;
				goto end;
			}

			break;
		}

		case 'l':
			lflag = 1;
			break;

		case 'm':
			mflag = 1;
			mnt = optarg;
			break;

		case 'g':
			gflag = 1;
			break;

		case 'x':
			xflag = 1;
			break;

		case 'o':
			oflag = 1;
			break;

		case '?':
			ret = -1;
			goto end;
		}
	}

	if (fsd_open(&handle) != 0)
		return (errout(&handle));

	if (eflag) {
		if (fsd_enable(&handle) != 0)
			ret = errout(&handle);

	} else if (dflag) {
		if (fsd_disable(&handle) != 0)
			ret = errout(&handle);

	} else if (lflag) {
		list();

	} else if (xflag) {
		if (oflag) {
			if (fsd_disturb_omni_off(&handle) != 0)
				ret = errout(&handle);

		} else if (mflag) {
			if (fsd_disturb_off(&handle, mnt) != 0)
				ret = errout(&handle);

		} else {
			(void) fprintf(stderr, "Don't know what to clear. "
			    "Use -o or -m PATH with -x option.\n");
		}

	} else if (gflag) {
		if (mflag) {
			if (fsd_get_param(&handle, mnt, &param) != 0) {
				ret = errout(&handle);
			} else {
				(void) printf("%s\n", mnt);
				print_fsd(&param);
			}
		} else {
			(void) fprintf(stderr, "Don't know what to get. "
			    "Use -m mountpoint with -g option.\n");
		}

	} else if (iflag) {	/* add other disturbances here */

		if (oflag) {
			if (fsd_disturb_omni(&handle, &param) != 0)
				ret = errout(&handle);

		} else if (mflag) {
			if (fsd_disturb(&handle, mnt, &param) != 0)
				ret = errout(&handle);

		} else {
			(void) fprintf(stderr,
			    "Don't know what to disturb. "
			    "Use -o or -m mountpoint with this option.\n");
		}

	} else {
		info();
	}

end:
	fsd_close(&handle);
	return (ret);

usage:
	(void) fprintf(stderr, "Usage:\n"
	    "\tfsdadm\n"
	    "\tfsdadm -e | -d\n"
	    "\tfsdadm -l\n"
	    "\tfsdadm [-m mountpoint | -o] [-x | -g]\n"
	    "\tfsdadm [-m mountpoint | -o] [-i type [params ...]] ...\n"
	    "\tfsdadm -h\n\n");

	return (ret);
}
