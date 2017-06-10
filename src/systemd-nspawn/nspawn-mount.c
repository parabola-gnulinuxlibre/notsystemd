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

#include <sys/mount.h>

#include <linux/magic.h>

#include "systemd-basic/alloc-util.h"
#include "systemd-basic/escape.h"
#include "systemd-basic/fd-util.h"
#include "systemd-basic/fileio.h"
#include "systemd-basic/fs-util.h"
#include "systemd-basic/label.h"
#include "systemd-basic/mkdir.h"
#include "systemd-basic/mount-util.h"
#include "systemd-basic/parse-util.h"
#include "systemd-basic/path-util.h"
#include "systemd-basic/rm-rf.h"
#include "systemd-basic/set.h"
#include "systemd-basic/stat-util.h"
#include "systemd-basic/string-util.h"
#include "systemd-basic/strv.h"
#include "systemd-basic/user-util.h"
#include "systemd-basic/util.h"

#include "nspawn-mount.h"

CustomMount* custom_mount_add(CustomMount **l, unsigned *n, CustomMountType t) {
        CustomMount *c, *ret;

        assert(l);
        assert(n);
        assert(t >= 0);
        assert(t < _CUSTOM_MOUNT_TYPE_MAX);

        c = realloc(*l, (*n + 1) * sizeof(CustomMount));
        if (!c)
                return NULL;

        *l = c;
        ret = *l + *n;
        (*n)++;

        *ret = (CustomMount) { .type = t };

        return ret;
}

void custom_mount_free_all(CustomMount *l, unsigned n) {
        unsigned i;

        for (i = 0; i < n; i++) {
                CustomMount *m = l + i;

                free(m->source);
                free(m->destination);
                free(m->options);

                if (m->work_dir) {
                        (void) rm_rf(m->work_dir, REMOVE_ROOT|REMOVE_PHYSICAL);
                        free(m->work_dir);
                }

                strv_free(m->lower);
        }

        free(l);
}

int custom_mount_compare(const void *a, const void *b) {
        const CustomMount *x = a, *y = b;
        int r;

        r = path_compare(x->destination, y->destination);
        if (r != 0)
                return r;

        if (x->type < y->type)
                return -1;
        if (x->type > y->type)
                return 1;

        return 0;
}

int bind_mount_parse(CustomMount **l, unsigned *n, const char *s, bool read_only) {
        _cleanup_free_ char *source = NULL, *destination = NULL, *opts = NULL;
        const char *p = s;
        CustomMount *m;
        int r;

        assert(l);
        assert(n);

        r = extract_many_words(&p, ":", EXTRACT_DONT_COALESCE_SEPARATORS, &source, &destination, NULL);
        if (r < 0)
                return r;
        if (r == 0)
                return -EINVAL;

        if (r == 1) {
                destination = strdup(source);
                if (!destination)
                        return -ENOMEM;
        }

        if (r == 2 && !isempty(p)) {
                opts = strdup(p);
                if (!opts)
                        return -ENOMEM;
        }

        if (!path_is_absolute(source))
                return -EINVAL;

        if (!path_is_absolute(destination))
                return -EINVAL;

        m = custom_mount_add(l, n, CUSTOM_MOUNT_BIND);
        if (!m)
                return log_oom();

        m->source = source;
        m->destination = destination;
        m->read_only = read_only;
        m->options = opts;

        source = destination = opts = NULL;
        return 0;
}

int tmpfs_mount_parse(CustomMount **l, unsigned *n, const char *s) {
        _cleanup_free_ char *path = NULL, *opts = NULL;
        const char *p = s;
        CustomMount *m;
        int r;

        assert(l);
        assert(n);
        assert(s);

        r = extract_first_word(&p, &path, ":", EXTRACT_DONT_COALESCE_SEPARATORS);
        if (r < 0)
                return r;
        if (r == 0)
                return -EINVAL;

        if (isempty(p))
                opts = strdup("mode=0755");
        else
                opts = strdup(p);
        if (!opts)
                return -ENOMEM;

        if (!path_is_absolute(path))
                return -EINVAL;

        m = custom_mount_add(l, n, CUSTOM_MOUNT_TMPFS);
        if (!m)
                return -ENOMEM;

        m->destination = path;
        m->options = opts;

        path = opts = NULL;
        return 0;
}

static int tmpfs_patch_options(
                const char *options,
                uid_t uid,
                const char *selinux_apifs_context,
                char **ret) {

        char *buf = NULL;

        if (uid >= 0) {
                if (options)
                        (void) asprintf(&buf, "%s,uid=" UID_FMT ",gid=" UID_FMT, options, uid, uid);
                else
                        (void) asprintf(&buf, "uid=" UID_FMT ",gid=" UID_FMT, uid, uid);
                if (!buf)
                        return -ENOMEM;

                options = buf;
        }

#ifdef HAVE_SELINUX
        if (selinux_apifs_context) {
                char *t;

                if (options)
                        t = strjoin(options, ",context=\"", selinux_apifs_context, "\"", NULL);
                else
                        t = strjoin("context=\"", selinux_apifs_context, "\"", NULL);
                if (!t) {
                        free(buf);
                        return -ENOMEM;
                }

                free(buf);
                buf = t;
        }
#endif

        if (!buf && options) {
                buf = strdup(options);
                if (!buf)
                        return -ENOMEM;
        }
        *ret = buf;

        return !!buf;
}

static int mount_sysfs(const char *dest) {
        const char *full, *top, *x;
        int r;

        top = prefix_roota(dest, "/sys");
        r = path_check_fstype(top, SYSFS_MAGIC);
        if (r < 0)
                return log_error_errno(r, "Failed to determine filesystem type of %s: %m", top);
        /* /sys might already be mounted as sysfs by the outer child in the
         * !netns case. In this case, it's all good. Don't touch it because we
         * don't have the right to do so, see https://github.com/systemd/systemd/issues/1555.
         */
        if (r > 0)
                return 0;

        full = prefix_roota(top, "/full");

        (void) mkdir(full, 0755);

        r = mount_verbose(LOG_ERR, "sysfs", full, "sysfs",
                          MS_RDONLY|MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL);
        if (r < 0)
                return r;

        FOREACH_STRING(x, "block", "bus", "class", "dev", "devices", "kernel") {
                _cleanup_free_ char *from = NULL, *to = NULL;

                from = prefix_root(full, x);
                if (!from)
                        return log_oom();

                to = prefix_root(top, x);
                if (!to)
                        return log_oom();

                (void) mkdir(to, 0755);

                r = mount_verbose(LOG_ERR, from, to, NULL, MS_BIND, NULL);
                if (r < 0)
                        return r;

                r = mount_verbose(LOG_ERR, NULL, to, NULL,
                                  MS_BIND|MS_RDONLY|MS_NOSUID|MS_NOEXEC|MS_NODEV|MS_REMOUNT, NULL);
                if (r < 0)
                        return r;
        }

        r = umount_verbose(full);
        if (r < 0)
                return r;

        if (rmdir(full) < 0)
                return log_error_errno(errno, "Failed to remove %s: %m", full);

        x = prefix_roota(top, "/fs/kdbus");
        (void) mkdir_p(x, 0755);

        /* We need to ensure that /sys/fs/cgroup exists before we remount /sys read-only.
         *
         * If !use_cgns, then this was already done by the outer child; so we only need to do it here it if use_cgns.
         * This function doesn't know whether use_cgns, but !cg_ns_supported()⇒!use_cgns, so we can "optimize" the case
         * where we _know_ !use_cgns, and deal with a no-op mkdir_p() in the false-positive where cgns_supported() but
         * !use_cgns.
         *
         * But is it really much of an optimization?  We're potentially spending an access(2) (cg_ns_supported() could
         * be cached from a previous call) to potentially save an lstat(2) and mkdir(2); and all of them are on virtual
         * fileystems, so they should all be pretty cheap. */
        if (cg_ns_supported()) { /* if (use_cgns) { */
                x = prefix_roota(top, "/fs/cgroup");
                (void) mkdir_p(x, 0755);
        }

        return mount_verbose(LOG_ERR, NULL, top, NULL,
                             MS_BIND|MS_RDONLY|MS_NOSUID|MS_NOEXEC|MS_NODEV|MS_REMOUNT, NULL);
}

static int mkdir_userns(const char *path, mode_t mode, bool in_userns, uid_t uid_shift) {
        int r;

        assert(path);

        r = mkdir(path, mode);
        if (r < 0 && errno != EEXIST)
                return -errno;

        if (!in_userns) {
                r = lchown(path, uid_shift, uid_shift);
                if (r < 0)
                        return -errno;
        }

        return 0;
}

static int mkdir_userns_p(const char *prefix, const char *path, mode_t mode, bool in_userns, uid_t uid_shift) {
        const char *p, *e;
        int r;

        assert(path);

        if (prefix && !path_startswith(path, prefix))
                return -ENOTDIR;

        /* create every parent directory in the path, except the last component */
        p = path + strspn(path, "/");
        for (;;) {
                char t[strlen(path) + 1];

                e = p + strcspn(p, "/");
                p = e + strspn(e, "/");

                /* Is this the last component? If so, then we're done */
                if (*p == 0)
                        break;

                memcpy(t, path, e - path);
                t[e-path] = 0;

                if (prefix && path_startswith(prefix, t))
                        continue;

                r = mkdir_userns(t, mode, in_userns, uid_shift);
                if (r < 0)
                        return r;
        }

        return mkdir_userns(path, mode, in_userns, uid_shift);
}

static int mount_all(const char *dest,
              bool use_userns, bool in_userns,
              bool use_netns,
              uid_t uid_shift, uid_t uid_range,
              const char *selinux_apifs_context) {

        typedef struct MountPoint {
                const char *what;
                const char *where;
                const char *type;
                const char *options;
                unsigned long flags;
                bool fatal;
                bool in_userns;
                bool use_netns;
        } MountPoint;

        static const MountPoint mount_table[] = {
                { "proc",                "/proc",               "proc",  NULL,        MS_NOSUID|MS_NOEXEC|MS_NODEV,                              true,  true,  false },
                { "/proc/sys",           "/proc/sys",           NULL,    NULL,        MS_BIND,                                                   true,  true,  false },   /* Bind mount first ...*/
                { "/proc/sys/net",       "/proc/sys/net",       NULL,    NULL,        MS_BIND,                                                   true,  true,  true  },   /* (except for this) */
                { NULL,                  "/proc/sys",           NULL,    NULL,        MS_BIND|MS_RDONLY|MS_NOSUID|MS_NOEXEC|MS_NODEV|MS_REMOUNT, true,  true,  false },   /* ... then, make it r/o */
                { "/proc/sysrq-trigger", "/proc/sysrq-trigger", NULL,    NULL,        MS_BIND,                                                   false, true,  false },   /* Bind mount first ...*/
                { NULL,                  "/proc/sysrq-trigger", NULL,    NULL,        MS_BIND|MS_RDONLY|MS_NOSUID|MS_NOEXEC|MS_NODEV|MS_REMOUNT, false, true,  false },   /* ... then, make it r/o */
                { "tmpfs",               "/sys",                "tmpfs", "mode=755",  MS_NOSUID|MS_NOEXEC|MS_NODEV,                              true,  false, true  },
                { "sysfs",               "/sys",                "sysfs", NULL,        MS_RDONLY|MS_NOSUID|MS_NOEXEC|MS_NODEV,                    true,  false, false },
                { "tmpfs",               "/dev",                "tmpfs", "mode=755",  MS_NOSUID|MS_STRICTATIME,                                  true,  false, false },
                { "tmpfs",               "/dev/shm",            "tmpfs", "mode=1777", MS_NOSUID|MS_NODEV|MS_STRICTATIME,                         true,  false, false },
                { "tmpfs",               "/run",                "tmpfs", "mode=755",  MS_NOSUID|MS_NODEV|MS_STRICTATIME,                         true,  false, false },
                { "tmpfs",               "/tmp",                "tmpfs", "mode=1777", MS_STRICTATIME,                                            true,  false,  false },
#ifdef HAVE_SELINUX
                { "/sys/fs/selinux",     "/sys/fs/selinux",     NULL,     NULL,       MS_BIND,                                                   false, false, false },  /* Bind mount first */
                { NULL,                  "/sys/fs/selinux",     NULL,     NULL,       MS_BIND|MS_RDONLY|MS_NOSUID|MS_NOEXEC|MS_NODEV|MS_REMOUNT, false, false, false },  /* Then, make it r/o */
#endif
        };

        unsigned k;
        int r;

        for (k = 0; k < ELEMENTSOF(mount_table); k++) {
                _cleanup_free_ char *where = NULL, *options = NULL;
                const char *o;

                if (in_userns != mount_table[k].in_userns)
                        continue;

                if (!use_netns && mount_table[k].use_netns)
                        continue;

                where = prefix_root(dest, mount_table[k].where);
                if (!where)
                        return log_oom();

                r = path_is_mount_point(where, AT_SYMLINK_FOLLOW);
                if (r < 0 && r != -ENOENT)
                        return log_error_errno(r, "Failed to detect whether %s is a mount point: %m", where);

                /* Skip this entry if it is not a remount. */
                if (mount_table[k].what && r > 0)
                        continue;

                r = mkdir_userns_p(dest, where, 0755, in_userns, uid_shift);
                if (r < 0 && r != -EEXIST) {
                        if (mount_table[k].fatal)
                                return log_error_errno(r, "Failed to create directory %s: %m", where);

                        log_debug_errno(r, "Failed to create directory %s: %m", where);
                        continue;
                }

                o = mount_table[k].options;
                if (streq_ptr(mount_table[k].type, "tmpfs")) {
                        r = tmpfs_patch_options(o, in_userns ? 0 : uid_shift, selinux_apifs_context, &options);
                        if (r < 0)
                                return log_oom();
                        if (r > 0)
                                o = options;
                }

                r = mount_verbose(mount_table[k].fatal ? LOG_ERR : LOG_WARNING,
                                  mount_table[k].what,
                                  where,
                                  mount_table[k].type,
                                  mount_table[k].flags,
                                  o);
                if (r < 0 && mount_table[k].fatal)
                        return r;
        }

        return 0;
}

int mount_post_userns(const char *dest,
                     bool use_userns,
                     bool use_netns,
                     uid_t uid_shift,
                     uid_t uid_range,
                     const char *selinux_apifs_context) {

        int r;

        r = mount_all(NULL, use_userns, false, use_netns, uid_shift, uid_range, selinux_apifs_context);
        if (r < 0)
                return r;

        r = mount_sysfs(NULL);
        if (r < 0)
                return r;

        return 0;
}

int mount_pre_userns(const char *dest,
                     bool use_userns,
                     bool use_netns,
                     uid_t uid_shift,
                     uid_t uid_range,
                     const char *selinux_apifs_context) {
        return mount_all(dest, use_userns, true, use_netns, uid_shift, uid_range, selinux_apifs_context);
}

static int parse_mount_bind_options(const char *options, unsigned long *mount_flags, char **mount_opts) {
        const char *p = options;
        unsigned long flags = *mount_flags;
        char *opts = NULL;

        assert(options);

        for (;;) {
                _cleanup_free_ char *word = NULL;
                int r = extract_first_word(&p, &word, ",", 0);
                if (r < 0)
                        return log_error_errno(r, "Failed to extract mount option: %m");
                if (r == 0)
                        break;

                if (streq(word, "rbind"))
                        flags |= MS_REC;
                else if (streq(word, "norbind"))
                        flags &= ~MS_REC;
                else {
                        log_error("Invalid bind mount option: %s", word);
                        return -EINVAL;
                }
        }

        *mount_flags = flags;
        /* in the future mount_opts will hold string options for mount(2) */
        *mount_opts = opts;

        return 0;
}

static int mount_bind(const char *dest, CustomMount *m) {
        struct stat source_st, dest_st;
        const char *where;
        unsigned long mount_flags = MS_BIND | MS_REC;
        _cleanup_free_ char *mount_opts = NULL;
        int r;

        assert(m);

        if (m->options) {
                r = parse_mount_bind_options(m->options, &mount_flags, &mount_opts);
                if (r < 0)
                        return r;
        }

        if (stat(m->source, &source_st) < 0)
                return log_error_errno(errno, "Failed to stat %s: %m", m->source);

        where = prefix_roota(dest, m->destination);

        if (stat(where, &dest_st) >= 0) {
                if (S_ISDIR(source_st.st_mode) && !S_ISDIR(dest_st.st_mode)) {
                        log_error("Cannot bind mount directory %s on file %s.", m->source, where);
                        return -EINVAL;
                }

                if (!S_ISDIR(source_st.st_mode) && S_ISDIR(dest_st.st_mode)) {
                        log_error("Cannot bind mount file %s on directory %s.", m->source, where);
                        return -EINVAL;
                }

        } else if (errno == ENOENT) {
                r = mkdir_parents_label(where, 0755);
                if (r < 0)
                        return log_error_errno(r, "Failed to make parents of %s: %m", where);

                /* Create the mount point. Any non-directory file can be
                * mounted on any non-directory file (regular, fifo, socket,
                * char, block).
                */
                if (S_ISDIR(source_st.st_mode))
                        r = mkdir_label(where, 0755);
                else
                        r = touch(where);
                if (r < 0)
                        return log_error_errno(r, "Failed to create mount point %s: %m", where);

        } else
                return log_error_errno(errno, "Failed to stat %s: %m", where);

        r = mount_verbose(LOG_ERR, m->source, where, NULL, mount_flags, mount_opts);
        if (r < 0)
                return r;

        if (m->read_only) {
                r = bind_remount_recursive(where, true, NULL);
                if (r < 0)
                        return log_error_errno(r, "Read-only bind mount failed: %m");
        }

        return 0;
}

static int mount_tmpfs(
                const char *dest,
                CustomMount *m,
                uid_t uid_shift,
                const char *selinux_apifs_context) {

        const char *where, *options;
        _cleanup_free_ char *buf = NULL;
        int r;

        assert(dest);
        assert(m);

        where = prefix_roota(dest, m->destination);

        r = mkdir_p_label(where, 0755);
        if (r < 0 && r != -EEXIST)
                return log_error_errno(r, "Creating mount point for tmpfs %s failed: %m", where);

        r = tmpfs_patch_options(m->options, uid_shift, selinux_apifs_context, &buf);
        if (r < 0)
                return log_oom();
        options = r > 0 ? buf : m->options;

        return mount_verbose(LOG_ERR, "tmpfs", where, "tmpfs", MS_NODEV|MS_STRICTATIME, options);
}

static char *joined_and_escaped_lower_dirs(char * const *lower) {
        _cleanup_strv_free_ char **sv = NULL;

        sv = strv_copy(lower);
        if (!sv)
                return NULL;

        strv_reverse(sv);

        if (!strv_shell_escape(sv, ",:"))
                return NULL;

        return strv_join(sv, ":");
}

static int mount_overlay(const char *dest, CustomMount *m) {
        _cleanup_free_ char *lower = NULL;
        const char *where, *options;
        int r;

        assert(dest);
        assert(m);

        where = prefix_roota(dest, m->destination);

        r = mkdir_label(where, 0755);
        if (r < 0 && r != -EEXIST)
                return log_error_errno(r, "Creating mount point for overlay %s failed: %m", where);

        (void) mkdir_p_label(m->source, 0755);

        lower = joined_and_escaped_lower_dirs(m->lower);
        if (!lower)
                return log_oom();

        if (m->read_only) {
                _cleanup_free_ char *escaped_source = NULL;

                escaped_source = shell_escape(m->source, ",:");
                if (!escaped_source)
                        return log_oom();

                options = strjoina("lowerdir=", escaped_source, ":", lower);
        } else {
                _cleanup_free_ char *escaped_source = NULL, *escaped_work_dir = NULL;

                assert(m->work_dir);
                (void) mkdir_label(m->work_dir, 0700);

                escaped_source = shell_escape(m->source, ",:");
                if (!escaped_source)
                        return log_oom();
                escaped_work_dir = shell_escape(m->work_dir, ",:");
                if (!escaped_work_dir)
                        return log_oom();

                options = strjoina("lowerdir=", lower, ",upperdir=", escaped_source, ",workdir=", escaped_work_dir);
        }

        return mount_verbose(LOG_ERR, "overlay", where, "overlay", m->read_only ? MS_RDONLY : 0, options);
}

int mount_custom(
                const char *dest,
                CustomMount *mounts, unsigned n,
                uid_t uid_shift
                const char *selinux_apifs_context) {

        unsigned i;
        int r;

        assert(dest);

        for (i = 0; i < n; i++) {
                CustomMount *m = mounts + i;

                switch (m->type) {

                case CUSTOM_MOUNT_BIND:
                        r = mount_bind(dest, m);
                        break;

                case CUSTOM_MOUNT_TMPFS:
                        r = mount_tmpfs(dest, m, uid_shift, selinux_apifs_context);
                        break;

                case CUSTOM_MOUNT_OVERLAY:
                        r = mount_overlay(dest, m);
                        break;

                default:
                        assert_not_reached("Unknown custom mount type");
                }

                if (r < 0)
                        return r;
        }

        return 0;
}

int setup_volatile(
                const char *directory,
                VolatileMode mode,
                bool userns, uid_t uid_shift, uid_t uid_range,
                const char *selinux_apifs_context) {

        bool tmpfs_mounted = false, bind_mounted = false;
        char template[] = "/tmp/nspawn-volatile-XXXXXX";
        _cleanup_free_ char *buf = NULL;
        const char *f, *t, *options;
        int r;

        assert(directory);

        switch (mode) {
        default:
                assert_not_reached("Unrecognized VolatileMode");
                return -EINVAL;
        case VOLATILE_NO:
                return 0;
        case VOLATILE_STATE:
                /* --volatile=state means we simply overmount /var
                   with a tmpfs, and the rest read-only. */

                r = bind_remount_recursive(directory, true, NULL);
                if (r < 0)
                        return log_error_errno(r, "Failed to remount %s read-only: %m", directory);

                t = prefix_roota(directory, "/var");
                r = mkdir(t, 0755);
                if (r < 0 && errno != EEXIST)
                        return log_error_errno(errno, "Failed to create %s: %m", t);

                options = "mode=755";
                r = tmpfs_patch_options(options, uid_shift, selinux_apifs_context, &buf);
                if (r < 0)
                        return log_oom();
                if (r > 0)
                        options = buf;

                return mount_verbose(LOG_ERR, "tmpfs", t, "tmpfs", MS_STRICTATIME, options);
        case VOLATILE_YES:
                /* --volatile=yes means we mount a tmpfs to the root dir, and
                   the original /usr to use inside it, and that read-only. */

                if (!mkdtemp(template))
                        return log_error_errno(errno, "Failed to create temporary directory: %m");

                options = "mode=755";
                r = tmpfs_patch_options(options, uid_shift, selinux_apifs_context, &buf);
                if (r < 0)
                        return log_oom();
                if (r > 0)
                        options = buf;

                r = mount_verbose(LOG_ERR, "tmpfs", template, "tmpfs", MS_STRICTATIME, options);
                if (r < 0)
                        goto fail;

                tmpfs_mounted = true;

                f = prefix_roota(directory, "/usr");
                t = prefix_roota(template, "/usr");

                r = mkdir(t, 0755);
                if (r < 0 && errno != EEXIST) {
                        r = log_error_errno(errno, "Failed to create %s: %m", t);
                        goto fail;
                }

                r = mount_verbose(LOG_ERR, f, t, NULL, MS_BIND|MS_REC, NULL);
                if (r < 0)
                        goto fail;

                bind_mounted = true;

                r = bind_remount_recursive(t, true, NULL);
                if (r < 0) {
                        log_error_errno(r, "Failed to remount %s read-only: %m", t);
                        goto fail;
                }

                r = mount_verbose(LOG_ERR, template, directory, NULL, MS_MOVE, NULL);
                if (r < 0)
                        goto fail;

                (void) rmdir(template);

                return 0;

        fail:
                if (bind_mounted)
                        (void) umount_verbose(t);

                if (tmpfs_mounted)
                        (void) umount_verbose(template);
                (void) rmdir(template);
                return r;
        }
}

VolatileMode volatile_mode_from_string(const char *s) {
        int b;

        if (isempty(s))
                return _VOLATILE_MODE_INVALID;

        b = parse_boolean(s);
        if (b > 0)
                return VOLATILE_YES;
        if (b == 0)
                return VOLATILE_NO;

        if (streq(s, "state"))
                return VOLATILE_STATE;

        return _VOLATILE_MODE_INVALID;
}