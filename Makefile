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
include $(dir $(lastword $(MAKEFILE_LIST)))/config.mk
include $(topsrcdir)/build-aux/Makefile.head.mk

# intltoolize
files.src.gen += m4/intltool.m4
files.src.gen += po/Makefile.in.in
# autoreconf
files.src.gen += aclocal.m4
files.src.gen += automake.mk.in
files.src.gen += build-aux/compile
files.src.gen += build-aux/config.guess
files.src.gen += build-aux/config.sub
files.src.gen += build-aux/install-sh
files.src.gen += build-aux/ltmain.sh
files.src.gen += build-aux/missing
files.src.gen += m4/libtool.m4
files.src.gen += m4/ltoptions.m4
files.src.gen += m4/ltsugar.m4
files.src.gen += m4/ltversion.m4
files.src.gen += m4/lt~obsolete.m4
files.src.gen += config.h.in
files.src.gen += configure

nested.subdirs += man
nested.subdirs += src
nested.subdirs += test

include $(topsrcdir)/build-aux/Makefile.tail.mk