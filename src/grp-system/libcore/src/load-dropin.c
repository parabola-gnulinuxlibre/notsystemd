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


#include "core/load-fragment.h"
#include "core/unit.h"
#include "systemd-basic/log.h"
#include "systemd-basic/strv.h"
#include "systemd-basic/unit-name.h"
#include "systemd-shared/conf-parser.h"

#include "load-dropin.h"

static int add_dependency_consumer(
                UnitDependency dependency,
                const char *entry,
                const char* filepath,
                void *arg) {
        Unit *u = arg;
        int r;

        assert(u);

        r = unit_add_dependency_by_name(u, dependency, entry, filepath, true);
        if (r < 0)
                log_error_errno(r, "Cannot add dependency %s to %s, ignoring: %m", entry, u->id);

        return 0;
}

int unit_load_dropin(Unit *u) {
        _cleanup_strv_free_ char **l = NULL;
        Iterator i;
        char *t, **f;
        int r;

        assert(u);

        /* Load dependencies from supplementary drop-in directories */

        SET_FOREACH(t, u->names, i) {
                char **p;

                STRV_FOREACH(p, u->manager->lookup_paths.search_path) {
                        unit_file_process_dir(u->manager->unit_path_cache, *p, t, ".wants", UNIT_WANTS,
                                              add_dependency_consumer, u, NULL);
                        unit_file_process_dir(u->manager->unit_path_cache, *p, t, ".requires", UNIT_REQUIRES,
                                              add_dependency_consumer, u, NULL);
                }
        }

        r = unit_find_dropin_paths(u, &l);
        if (r <= 0)
                return 0;

        if (!u->dropin_paths) {
                u->dropin_paths = l;
                l = NULL;
        } else {
                r = strv_extend_strv(&u->dropin_paths, l, true);
                if (r < 0)
                        return log_oom();
        }

        STRV_FOREACH(f, u->dropin_paths) {
                config_parse(u->id, *f, NULL,
                             UNIT_VTABLE(u)->sections,
                             config_item_perf_lookup, load_fragment_gperf_lookup,
                             false, false, false, u);
        }

        u->dropin_mtime = now(CLOCK_REALTIME);

        return 0;
}