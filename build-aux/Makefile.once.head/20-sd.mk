#  -*- Mode: makefile; indent-tabs-mode: t -*-
#
#  This file is part of systemd.
#
#  Copyright 2010-2012 Lennart Poettering
#  Copyright 2010-2012 Kay Sievers
#  Copyright 2013 Zbigniew Jędrzejewski-Szmek
#  Copyright 2013 David Strauss
#  Copyright 2016 Luke Shumaker
#
#  systemd is free software; you can redistribute it and/or modify it
#  under the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation; either version 2.1 of the License, or
#  (at your option) any later version.
#
#  systemd is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
#  Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with systemd; If not, see <http://www.gnu.org/licenses/>.

mod.sd.description = (systemd) shared build rules
mod.sd.depends += am
define mod.sd.doc
# User variables:
#   (flags)
#   - `CFLAGS`
#   - `CPPFLAGS`
#   - `LDFLAGS`
#   - `LIBTOOLFLAGS`
#   (programs)
#   - `CC`
#   - `CCLD`
#   - `LIBTOOL`
#   - `MKDIR_P`
#   - `SED`
#   (verbosity)
#   - `V`
#   - `AM_V_*`
#   - `INTLTOOL_V_MERGE*`
#   (substitutions)$(foreach v,$(sort $(sd.substitution_keys)),$(at.nl)#   - $v)
# Inputs:
#   - Global variable    : `DEPDIR`
#   - Global variable    : `ENABLE_TESTS`
#   - Global variable    : `ENABLE_UNSAFE_TESTS`
#   - Global variable    : `OUR_CFLAGS`
#   - Global variable    : `OUR_CPPFLAGS`
#   - Global variable    : `OUR_LDFLAGS`
#   - Global variable    : `OUR_LIBTOOLFLAGS`
#   (Makefiles)
#   - Directory variable : `sd.CFLAGS`
#   - Directory variable : `sd.CPPFLAGS`
#   - Directory variable : `sd.LDFLAGS`
#   - Directory variable : `sd.LIBTOOLFLAGS`
#   - Directory variable : `sd.sed_files` (default based on `EXTRA_DIST`)
#   (am)
#   - Directory variable : `EXTRA_DIST`
#   - Directory variable : `am.out_PROGRAMS`
#   - Directory variable : `am.CFLAGS`
#   - Directory variable : `am.CPPFLAGS`
#   - Target variable    : `am.LDFLAGS`
# Outputs:
#   - Global variable    : `SHELL`
#   - Make setting       : `.DELETE_ON_ERROR`
#   - Make setting       : `.SECONDARY`
#   - Global variable    : `sd.substitutions`
#   - Global variable    : `sd.substitution_keys`
#   - Global variable    : `sd.sed_process`
#   - Directory variable : `files.out.int`
#   - Target variable    : `sd.ALL_CFLAGS`
#   - Target variable    : `sd.ALL_CPPFLAGS`
#   - Target variable    : `sd.ALL_LDFLAGS`
#   - Target variable    : `sd.ALL_LIBTOLFLAGS`
#   - Target variable    : `sd.COMPILE`
#   - Target variable    : `sd.LTCOMPILE`
#   - Target variable    : `sd.LINK`
#   - Target             : `$$(outdir)/%.o`
#   - Target             : `$$(outdir)/%.lo`
#   - Target             : `$$(outdir)/$$(DEPDIR)`
#   - Target             : `$$(outdir)/%.la`
#   - Target             : `$$(addprefix $$(outdir)/,$$(am.out_PROGRAMS))`
#   - Target             : `$$(outdir)/test-lib%-sym.c`
#   - Target             : `$$(outdir)/%-from-name.gperf`
#   - Target             : `$$(outdir)/%-from-name.h`
#   - Target             : `$$(addprefix $$(outdir)/,$$(sd.sed_files))`
#   - Target             : `$$(outdir)/%.c: $$(srcdir)/%.gperf`
#   - Target             : `$$(outdir)/%: $$(srcdir)/%.m4`
# ???:
#   - tests
#   - unsafe_tests
#   - TESTS

endef

tests ?=
unsafe_tests ?=
TESTS = $(if $(ENABLE_TESTS),$(tests) \
        $(if $(ENABLE_UNSAFE_TESTS),$(unsafe_tests)))

# Make behavior
SHELL = bash -o pipefail

.DELETE_ON_ERROR:
.SECONDARY:

# Autoconf
OUR_CPPFLAGS += -MT $@ -MD -MP -MF $(@D)/$(DEPDIR)/$(basename $(@F)).P$(patsubst .%,%,$(suffix $(@F)))
OUR_CPPFLAGS += -include $(topoutdir)/config.h
OUR_CPPFLAGS += $(sort -I$(@D) $(if $(<D),-I$(<D) -I$(call at.out2src,$(<D))))

sd.ALL_CFLAGS       = $(strip $(OUR_CFLAGS)       $(am.CFLAGS)       $(sd.CFLAGS)       $(CFLAGS)       )
sd.ALL_CPPFLAGS     = $(strip $(OUR_CPPFLAGS)     $(am.CPPFLAGS)     $(sd.CPPFLAGS)     $(CPPFLAGS)     )
sd.ALL_LDFLAGS      = $(strip $(OUR_LDFLAGS)      $(am.LDFLAGS)      $(sd.LDFLAGS)      $(LDFLAGS)      )
sd.ALL_LIBTOOLFLAGS = $(strip $(OUR_LIBTOOLFLAGS)                    $(sd.LIBTOOLFLAGS) $(LIBTOOLFLAGS) )

sd.COMPILE   = $(CC) $(sd.ALL_CPPFLAGS) $(sd.ALL_CFLAGS)
sd.LTCOMPILE = $(LIBTOOL) $(AM_V_lt) --tag=CC $(sd.ALL_LIBTOOLFLAGS) --mode=compile $(CC) $(sd.ALL_CPPFLAGS) $(sd.ALL_CFLAGS)
sd.LINK      = $(LIBTOOL) $(AM_V_lt) --tag=CC $(sd.ALL_LIBTOOLFLAGS) --mode=link $(CCLD) $(sd.ALL_CFLAGS) $(sd.ALL_LDFLAGS) -o $@

CC ?= c99
CCLD ?= c99
LIBTOOL ?= libtool

V ?=

AM_V_at ?= $(AM_V_at_$(V))
AM_V_at_ ?= $(AM_V_at_$(AM_DEFAULT_VERBOSITY))
AM_V_at_0 ?= @
AM_V_at_1 ?=

AM_V_M4 ?= $(AM_V_M4_$(V))
AM_V_M4_ ?= $(AM_V_M4_$(AM_DEFAULT_VERBOSITY))
AM_V_M4_0 ?= @echo "  M4      " $@;
AM_V_M4_1 ?=

AM_V_GPERF ?= $(AM_V_GPERF_$(V))
AM_V_GPERF_ ?= $(AM_V_GPERF_$(AM_DEFAULT_VERBOSITY))
AM_V_GPERF_0 ?= @echo "  GPERF   " $@;
AM_V_GPERF_1 ?=

AM_V_RM ?= $(AM_V_RM_$(V))
AM_V_RM_ ?= $(AM_V_RM_$(AM_DEFAULT_VERBOSITY))
AM_V_RM_0 ?= @echo "  RM      " $@;
AM_V_RM_1 ?=

AM_V_CC ?= $(AM_V_CC_$(V))
AM_V_CC_ ?= $(AM_V_CC_$(AM_DEFAULT_VERBOSITY))
AM_V_CC_0 ?= @echo "  CC      " $@;
AM_V_CC_1 ?=

AM_V_CCLD ?= $(AM_V_CCLD_$(V))
AM_V_CCLD_ ?= $(AM_V_CCLD_$(AM_DEFAULT_VERBOSITY))
AM_V_CCLD_0 ?= @echo "  CCLD    " $@;
AM_V_CCLD_1 ?=

AM_V_EFI_CC ?= $(AM_V_EFI_CC_$(V))
AM_V_EFI_CC_ ?= $(AM_V_EFI_CC_$(AM_DEFAULT_VERBOSITY))
AM_V_EFI_CC_0 ?= @echo "  EFI_CC  " $@;
AM_V_EFI_CC_1 ?=

AM_V_EFI_CCLD ?= $(AM_V_EFI_CCLD_$(V))
AM_V_EFI_CCLD_ ?= $(AM_V_EFI_CCLD_$(AM_DEFAULT_VERBOSITY))
AM_V_EFI_CCLD_0 ?= @echo "  EFI_CCLD" $@;
AM_V_EFI_CCLD_1 ?=

AM_V_P ?= $(AM_V_P_$(V))
AM_V_P_ ?= $(AM_V_P_$(AM_DEFAULT_VERBOSITY))
AM_V_P_0 ?= false
AM_V_P_1 ?= :

AM_V_GEN ?= $(AM_V_GEN_$(V))
AM_V_GEN_ ?= $(AM_V_GEN_$(AM_DEFAULT_VERBOSITY))
AM_V_GEN_0 ?= @echo "  GEN     " $@;
AM_V_GEN_1 ?=

AM_V_lt ?= $(AM_V_lt_$(V))
AM_V_lt_ ?= $(AM_V_lt_$(AM_DEFAULT_VERBOSITY))
AM_V_lt_0 ?= --silent
AM_V_lt_1 ?=

INTLTOOL_V_MERGE ?= $(INTLTOOL_V_MERGE_$(V))
INTLTOOL_V_MERGE_OPTIONS ?= $(intltool_v_merge_options_$(V))
INTLTOOL_V_MERGE_ ?= $(INTLTOOL_V_MERGE_$(AM_DEFAULT_VERBOSITY))
INTLTOOL_V_MERGE_0 ?= @echo "  ITMRG " $@;
INTLTOOL_V_MERGE_1 ?=

sd.substitutions = \
       '|rootlibexecdir=$(rootlibexecdir)|' \
       '|rootbindir=$(rootbindir)|' \
       '|bindir=$(bindir)|' \
       '|SYSTEMCTL=$(rootbindir)/systemctl|' \
       '|SYSTEMD_NOTIFY=$(rootbindir)/systemd-notify|' \
       '|pkgsysconfdir=$(pkgsysconfdir)|' \
       '|SYSTEM_CONFIG_UNIT_PATH=$(pkgsysconfdir)/system|' \
       '|USER_CONFIG_UNIT_PATH=$(pkgsysconfdir)/user|' \
       '|pkgdatadir=$(pkgdatadir)|' \
       '|systemunitdir=$(systemunitdir)|' \
       '|userunitdir=$(userunitdir)|' \
       '|systempresetdir=$(systempresetdir)|' \
       '|userpresetdir=$(userpresetdir)|' \
       '|udevhwdbdir=$(udevhwdbdir)|' \
       '|udevrulesdir=$(udevrulesdir)|' \
       '|catalogdir=$(catalogdir)|' \
       '|tmpfilesdir=$(tmpfilesdir)|' \
       '|sysusersdir=$(sysusersdir)|' \
       '|sysctldir=$(sysctldir)|' \
       '|systemgeneratordir=$(systemgeneratordir)|' \
       '|usergeneratordir=$(usergeneratordir)|' \
       '|CERTIFICATEROOT=$(CERTIFICATEROOT)|' \
       '|PACKAGE_VERSION=$(PACKAGE_VERSION)|' \
       '|PACKAGE_NAME=$(PACKAGE_NAME)|' \
       '|PACKAGE_URL=$(PACKAGE_URL)|' \
       '|RANDOM_SEED_DIR=$(localstatedir)/lib/systemd/|' \
       '|RANDOM_SEED=$(localstatedir)/lib/systemd/random-seed|' \
       '|prefix=$(prefix)|' \
       '|exec_prefix=$(exec_prefix)|' \
       '|libdir=$(libdir)|' \
       '|includedir=$(includedir)|' \
       '|VERSION=$(VERSION)|' \
       '|rootprefix=$(rootprefix)|' \
       '|udevlibexecdir=$(udevlibexecdir)|' \
       '|SUSHELL=$(SUSHELL)|' \
       '|SULOGIN=$(SULOGIN)|' \
       '|DEBUGTTY=$(DEBUGTTY)|' \
       '|KILL=$(KILL)|' \
       '|KMOD=$(KMOD)|' \
       '|MOUNT_PATH=$(MOUNT_PATH)|' \
       '|UMOUNT_PATH=$(UMOUNT_PATH)|' \
       '|MKDIR_P=$(MKDIR_P)|' \
       '|QUOTAON=$(QUOTAON)|' \
       '|QUOTACHECK=$(QUOTACHECK)|' \
       '|SYSTEM_SYSVINIT_PATH=$(sysvinitdir)|' \
       '|VARLOGDIR=$(varlogdir)|' \
       '|RC_LOCAL_SCRIPT_PATH_START=$(RC_LOCAL_SCRIPT_PATH_START)|' \
       '|RC_LOCAL_SCRIPT_PATH_STOP=$(RC_LOCAL_SCRIPT_PATH_STOP)|' \
       '|PYTHON=$(PYTHON)|' \
       '|NTP_SERVERS=$(NTP_SERVERS)|' \
       '|DNS_SERVERS=$(DNS_SERVERS)|' \
       '|DEFAULT_DNSSEC_MODE=$(DEFAULT_DNSSEC_MODE)|' \
       '|KILL_USER_PROCESSES=$(KILL_USER_PROCESSES)|' \
       '|systemuidmax=$(SYSTEM_UID_MAX)|' \
       '|systemgidmax=$(SYSTEM_GID_MAX)|' \
       '|TTY_GID=$(TTY_GID)|' \
       '|systemsleepdir=$(systemsleepdir)|' \
       '|systemshutdowndir=$(systemshutdowndir)|' \
       '|binfmtdir=$(binfmtdir)|' \
       '|modulesloaddir=$(modulesloaddir)|'

sd.substitution_keys := $(subst |,,$(shell printf '%s\n' $(sd.substitutions) | cut -d= -f1))

sd.SED_PROCESS = \
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && \
	$(SED) $(subst '|,-e 's|@,$(subst =,\@|,$(subst |',|g',$(sd.substitutions)))) \
		< $< > $@