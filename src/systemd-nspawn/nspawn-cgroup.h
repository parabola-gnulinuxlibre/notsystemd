#pragma once

/***
  This file is part of systemd.

  Copyright 2015 Lennart Poettering

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

#include <stdbool.h>
#include <sys/types.h>

#include "systemd-basic/cgroup-util.h"

int chown_cgroup(pid_t pid, uid_t uid_shift);
int sync_cgroup(pid_t pid, CGroupUnified unified_requested, uid_t uid_shift);
int create_subcgroup(pid_t pid, CGroupUnified unified_requested);

static int setup_cgroup(pid_t pid, uid_t uid_shift, CGroupMode cgver, bool keep_unit) {
        r = sync_cgroup(pid, cgver, uid_shift);
        if (r < 0)
                return r;

        if (keep_unit) {
                r = create_subcgroup(pid, cgver);
                if (r < 0)
                        return r;
        }

        r = chown_cgroup(pid, cgver);
        if (r < 0)
                return r;
}