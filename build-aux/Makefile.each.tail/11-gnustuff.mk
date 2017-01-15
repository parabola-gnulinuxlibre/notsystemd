# Copyright (C) 2016-2017  Luke Shumaker
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
$(foreach d,$(gnustuff.program_dirs),$(eval $(call _gnustuff.install_program,$d)))
$(foreach d,$(gnustuff.data_dirs)   ,$(eval $(call _gnustuff.install_data,$d)))

$(outdir)/install-strip: install
	$(STRIP) $(filter $(addsuffix /%,$(gnustuff.program_dirs)),$(std.sys_files/$(@D)))

#TAGS: TODO
#check: TODO
#installcheck: TODO
