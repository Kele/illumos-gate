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

	if (info.fsdinf_enabled) {
		(void) printf(
		    "Enabled: yes\n"
		    "Filesystems disturbed: %d\n", (int)info.fsdinf_count);

		if (info.fsdinf_omni_on) {
			(void) printf(
			    "Omnipresent disturbing: yes\n"
			    "Omnipresent params:\n");
			print_fsd(&info.fsdinf_omni_param);
		} else {
			(void) printf("Omnipresent disturbing: no\n");
		}
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

int aflag;
int cflag;
int dflag;
int eflag;
int gflag;
int iflag;
int lflag;
int mflag;
int oflag;
int rflag;
int xflag;

char *mnt;
fsd_t param;
int chance;
int range[2];

int
main(int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	int opt;

	if (argc < 2)
		goto usage;

	if (fsd_open(&handle) != 0)
		return (errout(&handle));

	while ((opt = getopt(argc, argv, "ediam:gxoc:r:l")) != -1) {
		switch (opt) {
		case 'e':
			eflag = 1;
			break;

		case 'd':
			dflag = 1;
			break;

		case 'i':
			iflag = 1;
			break;

		case 'a':
			aflag = 1;
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

		case 'c':
			cflag = 1;
			chance = atoi(optarg);
			break;

		case 'r':
			rflag = 1;
			if (optind > argc - 1) {
				(void) fprintf(stderr,
				    "Error: -r requires two arguments\n");
				ret = -1;
				goto end;
			}
			range[0] = atoi(argv[optind-1]);
			range[1] = atoi(argv[optind]);
			optind++;
			break;

		case 'l':
			lflag = 1;
			break;

		case '?':
			(void) fprintf(stderr,
			    "Error: Unrecognized option: -%c\n", optopt);
			ret = -1;
			goto end;
		}
	}

	if (eflag) {
		if (fsd_enable(&handle) != 0)
			ret = errout(&handle);

	} else if (dflag) {
		if (fsd_disable(&handle) != 0)
			ret = errout(&handle);

	} else if (iflag) {
		info();

	} else if (aflag) {
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
			    "Use -m PATH with -g option.\n");
		}

	} else if (lflag) {	/* add other disturbances here */
		if (!(cflag && rflag)) {
			(void) fprintf(stderr, "Need chance and range.");
			goto end;
		}

		param.read_less_chance = chance;
		param.read_less_r[0] = range[0];
		param.read_less_r[1] = range[1];

		if (oflag) {
			if (fsd_disturb_omni(&handle, &param) != 0)
				ret = errout(&handle);

		} else if (mflag) {
			if (fsd_disturb(&handle, mnt, &param) != 0)
				ret = errout(&handle);

		} else {
			(void) fprintf(stderr,
			    "Don't know what to disturb. "
			    "Use -o or -m PATH with this options.");
		}

	} else {
usage:
		(void) fprintf(stderr, "Usage: fsdadm "
		    "[-ed] [-ai] [-o] [-x] [-g] [-l] "
		    "[-r range_start range_end]\n"
		    "\t[-c chance] [-m path]\n\n");

		(void) fprintf(stderr,
		    "\t -e enable fsd\n"
		    "\t -d disable fsd\n"
		    "\t -a display disturbance parameters for all disturbed\n"
		    "\t    filesystems\n"
		    "\t -i display information about current fsd status\n"
		    "\t -o omnipresent switch\n"
		    "\t -x clear switch\n"
		    "\t -g get disturbance parameters\n"
		    "\t -l \"read less\" disturbance\n"
		    "\t    every read operation would read n (from a given\n"
		    "\t    range) bytes less than it was requested\n"
		    "\t -r range for some types of disturbances\n"
		    "\t -c chance of the disturbance\n"
		    "\t -m path to mountpoint (or a representative file)\n"
		    "\n");
	}

end:
	fsd_close(&handle);
	return (ret);
}
