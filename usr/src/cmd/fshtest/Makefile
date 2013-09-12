#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2013 Damian Bogel. All rights reserved.
#

SUBDIRS = fshtestadm

include ../Makefile.cmd

all :=		TARGET = all
check :=	TARGET = check
clean :=	TARGET = clean
clobber :=	TARGET = clobber
install :=	TARGET = install
lint :=		TARGET = lint

all check clean clobber install lint: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
