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
include $(dir $(lastword $(MAKEFILE_LIST)))/../../config.mk
include $(topsrcdir)/build-aux/Makefile.head.mk

%-from-name.gperf: %-list.txt
	$(AM_V_at)$(MKDIR_P) $(dir $@)
	$(AM_V_GEN)$(AWK) 'BEGIN{ print "struct $(notdir $*)_name { const char* name; int id; };"; print "%null-strings"; print "%%";} { printf "%s, %s\n", $$1, $$1 }' <$< >$@

%-from-name.h: %-from-name.gperf
	$(AM_V_at)$(MKDIR_P) $(dir $@)
	$(AM_V_GPERF)$(GPERF) -L ANSI-C -t --ignore-case -N lookup_$(notdir $*) -H hash_$(notdir $*)_name -p -C <$< >$@

$(outdir)/%: sysctl.d/%.in
	$(SED_PROCESS)

%.sh: %.sh.in
	$(SED_PROCESS)
	$(AM_V_GEN)chmod +x $@

$(outdir)/%.c: src/%.gperf
	$(AM_V_at)$(MKDIR_P) $(dir $@)
	$(AM_V_GPERF)$(GPERF) < $< > $@

$(outdir)/%: src/%.m4 $(top_builddir)/config.status
	$(AM_V_at)$(MKDIR_P) $(dir $@)
	$(AM_V_M4)$(M4) -P $(M4_DEFINES) < $< > $@

$(outdir)/%.1: man/%.xml man/custom-man.xsl man/custom-entities.ent
	$(XSLTPROC_PROCESS_MAN)

$(outdir)/%.3: man/%.xml man/custom-man.xsl man/custom-entities.ent
	$(XSLTPROC_PROCESS_MAN)

$(outdir)/%.5: man/%.xml man/custom-man.xsl man/custom-entities.ent
	$(XSLTPROC_PROCESS_MAN)

$(outdir)/%.7: man/%.xml man/custom-man.xsl man/custom-entities.ent
	$(XSLTPROC_PROCESS_MAN)

$(outdir)/%.8: man/%.xml man/custom-man.xsl man/custom-entities.ent
	$(XSLTPROC_PROCESS_MAN)

$(outdir)/%.html: man/%.xml man/custom-html.xsl man/custom-entities.ent
	$(XSLTPROC_PROCESS_HTML)

include $(topsrcdir)/build-aux/Makefile.tail.mk
