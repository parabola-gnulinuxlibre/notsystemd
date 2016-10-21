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

#include <errno.h>

#include "systemd-basic/alloc-util.h"
#include "systemd-basic/fd-util.h"
#include "systemd-basic/fileio.h"
#include "systemd-basic/hashmap.h"
#include "systemd-basic/log.h"
#include "systemd-basic/mkdir.h"
#include "systemd-basic/parse-util.h"
#include "systemd-basic/path-util.h"
#include "systemd-basic/proc-cmdline.h"
#include "systemd-basic/string-util.h"
#include "systemd-basic/strv.h"
#include "systemd-basic/unit-name.h"
#include "systemd-basic/util.h"
#include "systemd-shared/dropin.h"
#include "systemd-shared/fstab-util.h"
#include "systemd-shared/generator.h"

typedef struct crypto_device {
        char *uuid;
        char *keyfile;
        char *name;
        char *options;
        bool create;
} crypto_device;

static const char *arg_dest = "/tmp";
static bool arg_enabled = true;
static bool arg_read_crypttab = true;
static bool arg_whitelist = false;
static Hashmap *arg_disks = NULL;
static char *arg_default_options = NULL;
static char *arg_default_keyfile = NULL;

static int create_disk(
                const char *name,
                const char *device,
                const char *password,
                const char *options) {

        _cleanup_free_ char *p = NULL, *n = NULL, *d = NULL, *u = NULL, *to = NULL, *e = NULL,
                *filtered = NULL;
        _cleanup_fclose_ FILE *f = NULL;
        bool noauto, nofail, tmp, swap;
        char *from;
        int r;

        assert(name);
        assert(device);

        noauto = fstab_test_yes_no_option(options, "noauto\0" "auto\0");
        nofail = fstab_test_yes_no_option(options, "nofail\0" "fail\0");
        tmp = fstab_test_option(options, "tmp\0");
        swap = fstab_test_option(options, "swap\0");

        if (tmp && swap) {
                log_error("Device '%s' cannot be both 'tmp' and 'swap'. Ignoring.", name);
                return -EINVAL;
        }

        e = unit_name_escape(name);
        if (!e)
                return log_oom();

        r = unit_name_build("systemd-cryptsetup", e, ".service", &n);
        if (r < 0)
                return log_error_errno(r, "Failed to generate unit name: %m");

        p = strjoin(arg_dest, "/", n, NULL);
        if (!p)
                return log_oom();

        u = fstab_node_to_udev_node(device);
        if (!u)
                return log_oom();

        r = unit_name_from_path(u, ".device", &d);
        if (r < 0)
                return log_error_errno(r, "Failed to generate unit name: %m");

        f = fopen(p, "wxe");
        if (!f)
                return log_error_errno(errno, "Failed to create unit file %s: %m", p);

        fputs(
                "# Automatically generated by systemd-cryptsetup-generator\n\n"
                "[Unit]\n"
                "Description=Cryptography Setup for %I\n"
                "Documentation=man:crypttab(5) man:systemd-cryptsetup-generator(8) man:systemd-cryptsetup@.service(8)\n"
                "SourcePath=/etc/crypttab\n"
                "DefaultDependencies=no\n"
                "Conflicts=umount.target\n"
                "BindsTo=dev-mapper-%i.device\n"
                "IgnoreOnIsolate=true\n"
                "After=cryptsetup-pre.target\n",
                f);

        if (!nofail)
                fprintf(f,
                        "Before=cryptsetup.target\n");

        if (password) {
                if (STR_IN_SET(password, "/dev/urandom", "/dev/random", "/dev/hw_random"))
                        fputs("After=systemd-random-seed.service\n", f);
                else if (!streq(password, "-") && !streq(password, "none")) {
                        _cleanup_free_ char *uu;

                        uu = fstab_node_to_udev_node(password);
                        if (!uu)
                                return log_oom();

                        if (!path_equal(uu, "/dev/null")) {

                                if (is_device_path(uu)) {
                                        _cleanup_free_ char *dd = NULL;

                                        r = unit_name_from_path(uu, ".device", &dd);
                                        if (r < 0)
                                                return log_error_errno(r, "Failed to generate unit name: %m");

                                        fprintf(f, "After=%1$s\nRequires=%1$s\n", dd);
                                } else
                                        fprintf(f, "RequiresMountsFor=%s\n", password);
                        }
                }
        }

        if (is_device_path(u))
                fprintf(f,
                        "BindsTo=%s\n"
                        "After=%s\n"
                        "Before=umount.target\n",
                        d, d);
        else
                fprintf(f,
                        "RequiresMountsFor=%s\n",
                        u);

        r = generator_write_timeouts(arg_dest, device, name, options, &filtered);
        if (r < 0)
                return r;

        fprintf(f,
                "\n[Service]\n"
                "Type=oneshot\n"
                "RemainAfterExit=yes\n"
                "TimeoutSec=0\n" /* the binary handles timeouts anyway */
                "ExecStart=" SYSTEMD_CRYPTSETUP_PATH " attach '%s' '%s' '%s' '%s'\n"
                "ExecStop=" SYSTEMD_CRYPTSETUP_PATH " detach '%s'\n",
                name, u, strempty(password), strempty(filtered),
                name);

        if (tmp)
                fprintf(f,
                        "ExecStartPost=/sbin/mke2fs '/dev/mapper/%s'\n",
                        name);

        if (swap)
                fprintf(f,
                        "ExecStartPost=/sbin/mkswap '/dev/mapper/%s'\n",
                        name);

        r = fflush_and_check(f);
        if (r < 0)
                return log_error_errno(r, "Failed to write file %s: %m", p);

        from = strjoina("../", n);

        if (!noauto) {

                to = strjoin(arg_dest, "/", d, ".wants/", n, NULL);
                if (!to)
                        return log_oom();

                mkdir_parents_label(to, 0755);
                if (symlink(from, to) < 0)
                        return log_error_errno(errno, "Failed to create symlink %s: %m", to);

                free(to);
                if (!nofail)
                        to = strjoin(arg_dest, "/cryptsetup.target.requires/", n, NULL);
                else
                        to = strjoin(arg_dest, "/cryptsetup.target.wants/", n, NULL);
                if (!to)
                        return log_oom();

                mkdir_parents_label(to, 0755);
                if (symlink(from, to) < 0)
                        return log_error_errno(errno, "Failed to create symlink %s: %m", to);
        }

        free(to);
        to = strjoin(arg_dest, "/dev-mapper-", e, ".device.requires/", n, NULL);
        if (!to)
                return log_oom();

        mkdir_parents_label(to, 0755);
        if (symlink(from, to) < 0)
                return log_error_errno(errno, "Failed to create symlink %s: %m", to);

        if (!noauto && !nofail) {
                _cleanup_free_ char *dmname;
                dmname = strjoin("dev-mapper-", e, ".device", NULL);
                if (!dmname)
                        return log_oom();

                r = write_drop_in(arg_dest, dmname, 90, "device-timeout",
                                  "# Automatically generated by systemd-cryptsetup-generator \n\n"
                                  "[Unit]\nJobTimeoutSec=0");
                if (r < 0)
                        return log_error_errno(r, "Failed to write device drop-in: %m");
        }

        return 0;
}

static void free_arg_disks(void) {
        crypto_device *d;

        while ((d = hashmap_steal_first(arg_disks))) {
                free(d->uuid);
                free(d->keyfile);
                free(d->name);
                free(d->options);
                free(d);
        }

        hashmap_free(arg_disks);
}

static crypto_device *get_crypto_device(const char *uuid) {
        int r;
        crypto_device *d;

        assert(uuid);

        d = hashmap_get(arg_disks, uuid);
        if (!d) {
                d = new0(struct crypto_device, 1);
                if (!d)
                        return NULL;

                d->create = false;
                d->keyfile = d->options = d->name = NULL;

                d->uuid = strdup(uuid);
                if (!d->uuid) {
                        free(d);
                        return NULL;
                }

                r = hashmap_put(arg_disks, d->uuid, d);
                if (r < 0) {
                        free(d->uuid);
                        free(d);
                        return NULL;
                }
        }

        return d;
}

static int parse_proc_cmdline_item(const char *key, const char *value) {
        int r;
        crypto_device *d;
        _cleanup_free_ char *uuid = NULL, *uuid_value = NULL;

        if (STR_IN_SET(key, "luks", "rd.luks") && value) {

                r = parse_boolean(value);
                if (r < 0)
                        log_warning("Failed to parse luks switch %s. Ignoring.", value);
                else
                        arg_enabled = r;

        } else if (STR_IN_SET(key, "luks.crypttab", "rd.luks.crypttab") && value) {

                r = parse_boolean(value);
                if (r < 0)
                        log_warning("Failed to parse luks crypttab switch %s. Ignoring.", value);
                else
                        arg_read_crypttab = r;

        } else if (STR_IN_SET(key, "luks.uuid", "rd.luks.uuid") && value) {

                d = get_crypto_device(startswith(value, "luks-") ? value+5 : value);
                if (!d)
                        return log_oom();

                d->create = arg_whitelist = true;

        } else if (STR_IN_SET(key, "luks.options", "rd.luks.options") && value) {

                r = sscanf(value, "%m[0-9a-fA-F-]=%ms", &uuid, &uuid_value);
                if (r == 2) {
                        d = get_crypto_device(uuid);
                        if (!d)
                                return log_oom();

                        free(d->options);
                        d->options = uuid_value;
                        uuid_value = NULL;
                } else if (free_and_strdup(&arg_default_options, value) < 0)
                        return log_oom();

        } else if (STR_IN_SET(key, "luks.key", "rd.luks.key") && value) {

                r = sscanf(value, "%m[0-9a-fA-F-]=%ms", &uuid, &uuid_value);
                if (r == 2) {
                        d = get_crypto_device(uuid);
                        if (!d)
                                return log_oom();

                        free(d->keyfile);
                        d->keyfile = uuid_value;
                        uuid_value = NULL;
                } else if (free_and_strdup(&arg_default_keyfile, value) < 0)
                        return log_oom();

        } else if (STR_IN_SET(key, "luks.name", "rd.luks.name") && value) {

                r = sscanf(value, "%m[0-9a-fA-F-]=%ms", &uuid, &uuid_value);
                if (r == 2) {
                        d = get_crypto_device(uuid);
                        if (!d)
                                return log_oom();

                        d->create = arg_whitelist = true;

                        free(d->name);
                        d->name = uuid_value;
                        uuid_value = NULL;
                } else
                        log_warning("Failed to parse luks name switch %s. Ignoring.", value);

        }

        return 0;
}

static int add_crypttab_devices(void) {
        struct stat st;
        unsigned crypttab_line = 0;
        _cleanup_fclose_ FILE *f = NULL;

        if (!arg_read_crypttab)
                return 0;

        f = fopen("/etc/crypttab", "re");
        if (!f) {
                if (errno != ENOENT)
                        log_error_errno(errno, "Failed to open /etc/crypttab: %m");
                return 0;
        }

        if (fstat(fileno(f), &st) < 0) {
                log_error_errno(errno, "Failed to stat /etc/crypttab: %m");
                return 0;
        }

        for (;;) {
                int r, k;
                char line[LINE_MAX], *l, *uuid;
                crypto_device *d = NULL;
                _cleanup_free_ char *name = NULL, *device = NULL, *keyfile = NULL, *options = NULL;

                if (!fgets(line, sizeof(line), f))
                        break;

                crypttab_line++;

                l = strstrip(line);
                if (*l == '#' || *l == 0)
                        continue;

                k = sscanf(l, "%ms %ms %ms %ms", &name, &device, &keyfile, &options);
                if (k < 2 || k > 4) {
                        log_error("Failed to parse /etc/crypttab:%u, ignoring.", crypttab_line);
                        continue;
                }

                uuid = startswith(device, "UUID=");
                if (!uuid)
                        uuid = path_startswith(device, "/dev/disk/by-uuid/");
                if (!uuid)
                        uuid = startswith(name, "luks-");
                if (uuid)
                        d = hashmap_get(arg_disks, uuid);

                if (arg_whitelist && !d) {
                        log_info("Not creating device '%s' because it was not specified on the kernel command line.", name);
                        continue;
                }

                r = create_disk(name, device, keyfile, (d && d->options) ? d->options : options);
                if (r < 0)
                        return r;

                if (d)
                        d->create = false;
        }

        return 0;
}

static int add_proc_cmdline_devices(void) {
        int r;
        Iterator i;
        crypto_device *d;

        HASHMAP_FOREACH(d, arg_disks, i) {
                const char *options;
                _cleanup_free_ char *device = NULL;

                if (!d->create)
                        continue;

                if (!d->name) {
                        d->name = strappend("luks-", d->uuid);
                        if (!d->name)
                                return log_oom();
                }

                device = strappend("UUID=", d->uuid);
                if (!device)
                        return log_oom();

                if (d->options)
                        options = d->options;
                else if (arg_default_options)
                        options = arg_default_options;
                else
                        options = "timeout=0";

                r = create_disk(d->name, device, d->keyfile ?: arg_default_keyfile, options);
                if (r < 0)
                        return r;
        }

        return 0;
}

int main(int argc, char *argv[]) {
        int r = EXIT_FAILURE;

        if (argc > 1 && argc != 4) {
                log_error("This program takes three or no arguments.");
                return EXIT_FAILURE;
        }

        if (argc > 1)
                arg_dest = argv[1];

        log_set_target(LOG_TARGET_SAFE);
        log_parse_environment();
        log_open();

        umask(0022);

        arg_disks = hashmap_new(&string_hash_ops);
        if (!arg_disks)
                goto cleanup;

        r = parse_proc_cmdline(parse_proc_cmdline_item);
        if (r < 0) {
                log_warning_errno(r, "Failed to parse kernel command line, ignoring: %m");
                r = EXIT_FAILURE;
        }

        if (!arg_enabled) {
                r = EXIT_SUCCESS;
                goto cleanup;
        }

        if (add_crypttab_devices() < 0)
                goto cleanup;

        if (add_proc_cmdline_devices() < 0)
                goto cleanup;

        r = EXIT_SUCCESS;

cleanup:
        free_arg_disks();
        free(arg_default_options);
        free(arg_default_keyfile);

        return r;
}
