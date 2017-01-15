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

mod.gnudoc.description = GNU Info page support
mod.gnudoc.depends += files nested gnuconf
define mod.gnudoc.doc
# User variables (in addition to gnuconf):
#   - `TEXI2HTML ?= makeinfo --html`
#   - `TEXI2PDF  ?= texi2pdf`
#   - `TEXI2PS   ?= texi2dvi --ps`
# Inputs:
#   - Directory variable : `gnudoc.docs ?=`
# Outputs:
#   - Global variable    : `files.groups += html dvi pdf ps`
#   - Global variable    : `nested.targets += info`
#   - Directory variable : `files.src.gen`
#   - Directory variable : `files.out.{dvi,html,pdf,ps}`
#   - Directory variable : `files.sys.{dvi,html,pdf,ps,all}`
#   - .PHONY target      : `$(outdir)/info`
#   - .PHONY target      : `$(outdir)/install` (see below)
#   - Target             : `$(outdir)/%.info`
#   - Target             : `$(outdir)/%.dvi`
#   - target             : `$(outdir)/%.html`
#   - target             : `$(outdir)/%.pdf`
#   - Target             : `$(outdir)/%.ps`
#
# The The `gnudoc

# The module counts on the `$(outdir)/install` target being defined by
# `files`, but not having a rule that executes once the dependencies
# have been taken care of; it adds a "post-install" rule to add the
# info files to the index.
endef
mod.gnudoc.doc := $(value mod.gnudoc.doc)

TEXI2HTML ?= makeinfo --html
TEXI2PDF  ?= texi2pdf
TEXI2PS   ?= texi2dvi --ps

files.groups += html dvi pdf ps
nested.targets += info
