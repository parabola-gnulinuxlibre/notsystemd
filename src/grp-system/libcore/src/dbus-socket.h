#pragma once

/***
  This file is part of systemd.

  Copyright 2010 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <systemd/sd-bus.h>

#include "core/unit.h"

extern const sd_bus_vtable bus_socket_vtable[];

int bus_socket_set_property(Unit *u, const char *name, sd_bus_message *message, UnitSetPropertiesMode mode, sd_bus_error *error);
int bus_socket_commit_properties(Unit *u);