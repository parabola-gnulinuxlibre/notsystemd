/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include <stdbool.h>
#include <sys/types.h>

#include "cgroup-util.h"

typedef struct CGMount CGMount;
typedef struct CGMounts {
        CGMount *mounts;
        size_t n;
} CGMounts;

int cgroup_setup(pid_t pid, CGroupUnified outer_cgver, CGroupUnified inner_cgver, uid_t uid_shift, bool keep_unit);
int cgroup_mount_mounts(const char *dest, CGMounts mounts, bool use_cgns, uid_t uid_shift, const char *selinux_apifs_context);
void cgroup_free_mounts(CGMounts *mounts);

int mount_cgroups(const char *dest, CGroupUnified outer_cgver, CGroupUnified inner_cgver, bool userns, uid_t uid_shift, uid_t uid_range, const char *selinux_apifs_context, bool use_cgns);
int mount_systemd_cgroup_writable(const char *dest, CGroupUnified inner_cgver);
