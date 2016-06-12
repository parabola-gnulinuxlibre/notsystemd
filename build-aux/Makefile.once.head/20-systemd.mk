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

SHELL = bash -o pipefail

OUR_CPPFLAGS += -MT $@ -MD -MP -MF $(@D)/$(DEPDIR)/$(basename $(@F)).P$(patsubst .%,%,$(suffix $(@F)))
OUR_CPPFLAGS += -include $(topoutdir)/config.h
OUR_CPPFLAGS += $(if $(<D),-I$(<D)) -I$(@D)

at.dirlocal += systemd.CFLAGS systemd.CPPFLAGS systemd.LDFLAGS systemd.LIBTOOLFLAGS
ALL_CFLAGS       = $(OUR_CFLAGS)       $(am.CFLAGS/$(@D))       $(systemd.CFLAGS/$(@D))       $(CFLAGS)
ALL_CPPFLAGS     = $(OUR_CPPFLAGS)     $(am.CPPFLAGS/$(@D))     $(systemd.CPPFLAGS/$(@D))     $(CPPFLAGS)
ALL_LDFLAGS      = $(OUR_LDFLAGS)      $(am.LDFLAGS/$(@D))      $(systemd.LDFLAGS/$(@D))      $(LDFLAGS)
ALL_LIBTOOLFLAGS = $(OUR_LIBTOOLFLAGS) $(am.LIBTOOLFLAGS/$(@D)) $(systemd.LIBTOOLFLAGS/$(@D)) $(LIBTOOLFLAGS)

COMPILE   = $(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS)
LTCOMPILE = $(LIBTOOL) $(AM_V_lt) --tag=CC $(ALL_LIBTOOLFLAGS) --mode=compile $(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS)
LINK      = $(LIBTOOL) $(AM_V_lt) --tag=CC $(ALL_LIBTOOLFLAGS) --mode=link $(CCLD) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $@

SED_PROCESS = \
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && \
	$(SED) $(subst '|,-e 's|@,$(subst =,\@|,$(subst |',|g',$(substitutions)))) \
		< $< > $@

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# Keep the test-suite.log
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

V ?=

AM_V_M4 = $(AM_V_M4_$(V))
AM_V_M4_ = $(AM_V_M4_$(AM_DEFAULT_VERBOSITY))
AM_V_M4_0 = @echo "  M4      " $@;
AM_V_M4_1 =

AM_V_XSLT = $(AM_V_XSLT_$(V))
AM_V_XSLT_ = $(AM_V_XSLT_$(AM_DEFAULT_VERBOSITY))
AM_V_XSLT_0 = @echo "  XSLT    " $@;
AM_V_XSLT_1 =

AM_V_GPERF = $(AM_V_GPERF_$(V))
AM_V_GPERF_ = $(AM_V_GPERF_$(AM_DEFAULT_VERBOSITY))
AM_V_GPERF_0 = @echo "  GPERF   " $@;
AM_V_GPERF_1 =

AM_V_LN = $(AM_V_LN_$(V))
AM_V_LN_ = $(AM_V_LN_$(AM_DEFAULT_VERBOSITY))
AM_V_LN_0 = @echo "  LN      " $@;
AM_V_LN_1 =

AM_V_RM = $(AM_V_RM_$(V))
AM_V_RM_ = $(AM_V_RM_$(AM_DEFAULT_VERBOSITY))
AM_V_RM_0 = @echo "  RM      " $@;
AM_V_RM_1 =

AM_V_CC = $(AM_V_CC_$(V))
AM_V_CC_ = $(AM_V_CC_$(AM_DEFAULT_VERBOSITY))
AM_V_CC_0 = @echo "  CC      " $@;
AM_V_CC_1 =

AM_V_CCLD = $(AM_V_CCLD_$(V))
AM_V_CCLD_ = $(AM_V_CCLD_$(AM_DEFAULT_VERBOSITY))
AM_V_CCLD_0 = @echo "  CCLD    " $@;
AM_V_CCLD_1 =

AM_V_P = $(AM_V_P_$(V))
AM_V_P_ = $(AM_V_P_$(AM_DEFAULT_VERBOSITY))
AM_V_P_0 = false
AM_V_P_1 = :

AM_V_GEN = $(AM_V_GEN_$(V))
AM_V_GEN_ = $(AM_V_GEN_$(AM_DEFAULT_VERBOSITY))
AM_V_GEN_0 = @echo "  GEN     " $@;
AM_V_GEN_1 =

AM_V_at = $(AM_V_at_$(V))
AM_V_at_ = $(AM_V_at_$(AM_DEFAULT_VERBOSITY))
AM_V_at_0 = @
AM_V_at_1 =

AM_V_lt = $(AM_V_lt_$(V))
AM_V_lt_ = $(AM_V_lt_$(AM_DEFAULT_VERBOSITY))
AM_V_lt_0 = --silent
AM_V_lt_1 =

INTLTOOL_V_MERGE = $(INTLTOOL_V_MERGE_$(V))
INTLTOOL_V_MERGE_OPTIONS = $(intltool_v_merge_options_$(V))
INTLTOOL_V_MERGE_ = $(INTLTOOL_V_MERGE_$(AM_DEFAULT_VERBOSITY))
INTLTOOL_V_MERGE_0 = @echo "  ITMRG " $@;
INTLTOOL_V_MERGE_1 =

substitutions = \
       '|libexecdir=$(libexecdir)|' \
       '|bindir=$(bindir)|' \
       '|bindir=$(bindir)|' \
       '|SYSTEMCTL=$(bindir)/systemctl|' \
       '|SYSTEMD_NOTIFY=$(bindir)/systemd-notify|' \
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
       '|prefix=$(prefix)|' \
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

SED_PROCESS = \
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && \
	$(SED) $(subst '|,-e 's|@,$(subst =,\@|,$(subst |',|g',$(substitutions)))) \
		< $< > $@

# Stupid test that everything purported to be exported really is
define generate-sym-test
	$(AM_V_at)$(MKDIR_P) $(dir $@)
	$(AM_V_at)printf '#include <stdio.h>\n' > $@
	$(AM_V_at)printf '#include "%s"\n' $(notdir $(filter %.h, $^)) >> $@
	$(AM_V_at)printf 'void* functions[] = {\n' >> $@
	$(AM_V_GEN)sed -r -n 's/^ +([a-zA-Z0-9_]+);/\1,/p' $< >> $@
	$(AM_V_at)printf '};\nint main(void) {\n' >> $@
	$(AM_V_at)printf 'unsigned i; for (i=0;i<sizeof(functions)/sizeof(void*);i++) printf("%%p\\n", functions[i]);\n' >> $@
	$(AM_V_at)printf 'return 0; }\n' >> $@
endef

at.dirlocal += systemd.sed_files
