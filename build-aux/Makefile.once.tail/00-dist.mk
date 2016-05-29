# Copyright (C) 2015-2016  Luke Shumaker
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

_dist.copyfile = $(MKDIR_P) $(dir $2) && $(CP) -T $1 $2
_dist.addfile = $(call _dist.copyfile,$3,$2/$(call at.relto,$1,$3))
$(topoutdir)/$(dist.name)-$(dist.version): $(std.src_files/$(topoutdir)) $(std.gen_files/$(topoutdir))
	$(RM) -r $@
	@PS4='' && set -x && \
	$(MKDIR) $(@D)/tmp.$(@F).$$$$ && \
	$(foreach f,$^,$(call _dist.addfile,$(topsrcdir),$(@D)/tmp.$(@F).$$$$,$f) &&) \
	$(MV) $(@D)/tmp.$(@F).$$$$ $@ || $(RM) -r $(@D)/tmp.$(@F).$$$$

$(topoutdir)/$(dist.name)-$(dist.version).tar: $(topoutdir)/$(dist.name)-$(dist.version)
	$(TAR) cf $@ -C $(<D) $(<F)
$(topoutdir)/$(dist.name)-$(dist.version).tar.gz: $(topoutdir)/$(dist.name)-$(dist.version).tar
	$(GZIP) $(GZIPFLAGS) < $< > $@
