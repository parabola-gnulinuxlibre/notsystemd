# Copyright (C) 2016  Luke Shumaker
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

mod.mod.description = Display information about Autothing modules

# The trickery that is _mod.empty/_mod.space is from §6.2 of the GNU Make
# manual, "The Two Flavors of Variables".
_mod.empty :=
_mod.space := $(_mod.empty) #
undefine _mod.empty
# _mod.rest is equivalent to GMSL rest.
_mod.rest = $(wordlist 2,$(words $1),$1)

_mod.file2mod = $(foreach _mod.tmp,$(patsubst %.mk,%,$(notdir $1)),$(subst $(_mod.space),-,$(call _mod.rest,$(subst -, ,$(_mod.tmp)))))

_mod.modules := $(sort $(call _mod.file2mod,$(wildcard $(topsrcdir)/build-aux/Makefile.*/??-*.mk)))
undefine _mod.rest
undefine _mod.file2mod

$(eval $(foreach _mod.tmp,$(_mod.modules),\
  mod.$(_mod.tmp).description ?=$(at.nl)\
  mod.$(_mod.tmp).depends ?=$(at.nl)\
  mod.$(_mod.tmp).files ?=$(at.nl)))

_mod.quote-pattern = $(subst %,\%,$(subst \,\\,$1))
_mod.quote-shell-each = $(foreach _mod.tmp,$1,$(call _mod.quote-shell,$(_mod.tmp)))

# I put this as the last line in the file because it confuses Emacs syntax
# highlighting and makes the remainder of the file difficult to edit.
_mod.quote-shell = $(subst $(at.nl),'$$'\n'','$(subst ','\'',$1)')
