/***
  This file is part of systemd.

  Copyright 2014 Tom Gundersen <teg@jklm.no>

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

#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>

#include "af-list.h"
#include "alloc-util.h"
#include "dns-domain.h"
#include "fd-util.h"
#include "fileio-label.h"
#include "hostname-util.h"
#include "io-util.h"
#include "netlink-util.h"
#include "network-internal.h"
#include "ordered-set.h"
#include "parse-util.h"
#include "random-util.h"
#include "resolved-bus.h"
#include "resolved-conf.h"
#include "resolved-etc-hosts.h"
#include "resolved-llmnr.h"
#include "resolved-manager.h"
#include "resolved-mdns.h"
#include "resolved-resolv-conf.h"
#include "socket-util.h"
#include "string-table.h"
#include "string-util.h"
#include "utf8.h"

#define SEND_TIMEOUT_USEC (200 * USEC_PER_MSEC)

static int manager_process_link(sd_netlink *rtnl, sd_netlink_message *mm, void *userdata) {
        Manager *m = userdata;
        uint16_t type;
        Link *l;
        int ifindex, r;

        assert(rtnl);
        assert(m);
        assert(mm);

        r = sd_netlink_message_get_type(mm, &type);
        if (r < 0)
                goto fail;

        r = sd_rtnl_message_link_get_ifindex(mm, &ifindex);
        if (r < 0)
                goto fail;

        l = hashmap_get(m->links, INT_TO_PTR(ifindex));

        switch (type) {

        case RTM_NEWLINK:{
                bool is_new = !l;

                if (!l) {
                        r = link_new(m, &l, ifindex);
                        if (r < 0)
                                goto fail;
                }

                r = link_update_rtnl(l, mm);
                if (r < 0)
                        goto fail;

                r = link_update_monitor(l);
                if (r < 0)
                        goto fail;

                if (is_new)
                        log_debug("Found new link %i/%s", ifindex, l->name);

                break;
        }

        case RTM_DELLINK:
                if (l) {
                        log_debug("Removing link %i/%s", l->ifindex, l->name);
                        link_free(l);
                }

                break;
        }

        return 0;

fail:
        log_warning_errno(r, "Failed to process RTNL link message: %m");
        return 0;
}

static int manager_process_address(sd_netlink *rtnl, sd_netlink_message *mm, void *userdata) {
        Manager *m = userdata;
        union in_addr_union address;
        uint16_t type;
        int r, ifindex, family;
        LinkAddress *a;
        Link *l;

        assert(rtnl);
        assert(mm);
        assert(m);

        r = sd_netlink_message_get_type(mm, &type);
        if (r < 0)
                goto fail;

        r = sd_rtnl_message_addr_get_ifindex(mm, &ifindex);
        if (r < 0)
                goto fail;

        l = hashmap_get(m->links, INT_TO_PTR(ifindex));
        if (!l)
                return 0;

        r = sd_rtnl_message_addr_get_family(mm, &family);
        if (r < 0)
                goto fail;

        switch (family) {

        case AF_INET:
                r = sd_netlink_message_read_in_addr(mm, IFA_LOCAL, &address.in);
                if (r < 0) {
                        r = sd_netlink_message_read_in_addr(mm, IFA_ADDRESS, &address.in);
                        if (r < 0)
                                goto fail;
                }

                break;

        case AF_INET6:
                r = sd_netlink_message_read_in6_addr(mm, IFA_LOCAL, &address.in6);
                if (r < 0) {
                        r = sd_netlink_message_read_in6_addr(mm, IFA_ADDRESS, &address.in6);
                        if (r < 0)
                                goto fail;
                }

                break;

        default:
                return 0;
        }

        a = link_find_address(l, family, &address);

        switch (type) {

        case RTM_NEWADDR:

                if (!a) {
                        r = link_address_new(l, &a, family, &address);
                        if (r < 0)
                                return r;
                }

                r = link_address_update_rtnl(a, mm);
                if (r < 0)
                        return r;

                break;

        case RTM_DELADDR:
                link_address_free(a);
                break;
        }

        return 0;

fail:
        log_warning_errno(r, "Failed to process RTNL address message: %m");
        return 0;
}

static int manager_rtnl_listen(Manager *m) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL, *reply = NULL;
        sd_netlink_message *i;
        int r;

        assert(m);

        /* First, subscribe to interfaces coming and going */
        r = sd_netlink_open(&m->rtnl);
        if (r < 0)
                return r;

        r = sd_netlink_attach_event(m->rtnl, m->event, SD_EVENT_PRIORITY_IMPORTANT);
        if (r < 0)
                return r;

        r = sd_netlink_add_match(m->rtnl, RTM_NEWLINK, manager_process_link, m);
        if (r < 0)
                return r;

        r = sd_netlink_add_match(m->rtnl, RTM_DELLINK, manager_process_link, m);
        if (r < 0)
                return r;

        r = sd_netlink_add_match(m->rtnl, RTM_NEWADDR, manager_process_address, m);
        if (r < 0)
                return r;

        r = sd_netlink_add_match(m->rtnl, RTM_DELADDR, manager_process_address, m);
        if (r < 0)
                return r;

        /* Then, enumerate all links */
        r = sd_rtnl_message_new_link(m->rtnl, &req, RTM_GETLINK, 0);
        if (r < 0)
                return r;

        r = sd_netlink_message_request_dump(req, true);
        if (r < 0)
                return r;

        r = sd_netlink_call(m->rtnl, req, 0, &reply);
        if (r < 0)
                return r;

        for (i = reply; i; i = sd_netlink_message_next(i)) {
                r = manager_process_link(m->rtnl, i, m);
                if (r < 0)
                        return r;
        }

        req = sd_netlink_message_unref(req);
        reply = sd_netlink_message_unref(reply);

        /* Finally, enumerate all addresses, too */
        r = sd_rtnl_message_new_addr(m->rtnl, &req, RTM_GETADDR, 0, AF_UNSPEC);
        if (r < 0)
                return r;

        r = sd_netlink_message_request_dump(req, true);
        if (r < 0)
                return r;

        r = sd_netlink_call(m->rtnl, req, 0, &reply);
        if (r < 0)
                return r;

        for (i = reply; i; i = sd_netlink_message_next(i)) {
                r = manager_process_address(m->rtnl, i, m);
                if (r < 0)
                        return r;
        }

        return r;
}

static int on_network_event(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
        Manager *m = userdata;
        Iterator i;
        Link *l;
        int r;

        assert(m);

        sd_network_monitor_flush(m->network_monitor);

        HASHMAP_FOREACH(l, m->links, i) {
                r = link_update_monitor(l);
                if (r < 0)
                        log_warning_errno(r, "Failed to update monitor information for %i: %m", l->ifindex);
        }

        r = manager_write_resolv_conf(m);
        if (r < 0)
                log_warning_errno(r, "Could not update "PRIVATE_RESOLV_CONF": %m");

        return 0;
}

static int manager_network_monitor_listen(Manager *m) {
        int r, fd, events;

        assert(m);

        r = sd_network_monitor_new(&m->network_monitor, NULL);
        if (r < 0)
                return r;

        fd = sd_network_monitor_get_fd(m->network_monitor);
        if (fd < 0)
                return fd;

        events = sd_network_monitor_get_events(m->network_monitor);
        if (events < 0)
                return events;

        r = sd_event_add_io(m->event, &m->network_event_source, fd, events, &on_network_event, m);
        if (r < 0)
                return r;

        r = sd_event_source_set_priority(m->network_event_source, SD_EVENT_PRIORITY_IMPORTANT+5);
        if (r < 0)
                return r;

        (void) sd_event_source_set_description(m->network_event_source, "network-monitor");

        return 0;
}

static int determine_hostname(char **llmnr_hostname, char **mdns_hostname) {
        _cleanup_free_ char *h = NULL, *n = NULL;
        char label[DNS_LABEL_MAX];
        const char *p;
        int r, k;

        assert(llmnr_hostname);
        assert(mdns_hostname);

        /* Extract and normalize the first label of the locally
         * configured hostname, and check it's not "localhost". */

        h = gethostname_malloc();
        if (!h)
                return log_oom();

        p = h;
        r = dns_label_unescape(&p, label, sizeof(label));
        if (r < 0)
                return log_error_errno(r, "Failed to unescape host name: %m");
        if (r == 0) {
                log_error("Couldn't find a single label in hosntame.");
                return -EINVAL;
        }

        k = dns_label_undo_idna(label, r, label, sizeof(label));
        if (k < 0)
                return log_error_errno(k, "Failed to undo IDNA: %m");
        if (k > 0)
                r = k;

        if (!utf8_is_valid(label)) {
                log_error("System hostname is not UTF-8 clean.");
                return -EINVAL;
        }

        r = dns_label_escape_new(label, r, &n);
        if (r < 0)
                return log_error_errno(r, "Failed to escape host name: %m");

        if (is_localhost(n)) {
                log_debug("System hostname is 'localhost', ignoring.");
                return -EINVAL;
        }

        r = dns_name_concat(n, "local", mdns_hostname);
        if (r < 0)
                return log_error_errno(r, "Failed to determine mDNS hostname: %m");

        *llmnr_hostname = n;
        n = NULL;

        return 0;
}

static int on_hostname_change(sd_event_source *es, int fd, uint32_t revents, void *userdata) {
        _cleanup_free_ char *llmnr_hostname = NULL, *mdns_hostname = NULL;
        Manager *m = userdata;
        int r;

        assert(m);

        r = determine_hostname(&llmnr_hostname, &mdns_hostname);
        if (r < 0)
                return 0; /* ignore invalid hostnames */

        if (streq(llmnr_hostname, m->llmnr_hostname) && streq(mdns_hostname, m->mdns_hostname))
                return 0;

        log_info("System hostname changed to '%s'.", llmnr_hostname);

        free(m->llmnr_hostname);
        free(m->mdns_hostname);

        m->llmnr_hostname = llmnr_hostname;
        m->mdns_hostname = mdns_hostname;

        llmnr_hostname = mdns_hostname = NULL;

        manager_refresh_rrs(m);

        return 0;
}

static int manager_watch_hostname(Manager *m) {
        int r;

        assert(m);

        m->hostname_fd = open("/proc/sys/kernel/hostname", O_RDONLY|O_CLOEXEC|O_NDELAY|O_NOCTTY);
        if (m->hostname_fd < 0) {
                log_warning_errno(errno, "Failed to watch hostname: %m");
                return 0;
        }

        r = sd_event_add_io(m->event, &m->hostname_event_source, m->hostname_fd, 0, on_hostname_change, m);
        if (r < 0) {
                if (r == -EPERM)
                        /* kernels prior to 3.2 don't support polling this file. Ignore the failure. */
                        m->hostname_fd = safe_close(m->hostname_fd);
                else
                        return log_error_errno(r, "Failed to add hostname event source: %m");
        }

        (void) sd_event_source_set_description(m->hostname_event_source, "hostname");

        r = determine_hostname(&m->llmnr_hostname, &m->mdns_hostname);
        if (r < 0) {
                log_info("Defaulting to hostname 'gnu-linux'.");
                m->llmnr_hostname = strdup("gnu-linux");
                if (!m->llmnr_hostname)
                        return log_oom();

                m->mdns_hostname = strdup("gnu-linux.local");
                if (!m->mdns_hostname)
                        return log_oom();
        } else
                log_info("Using system hostname '%s'.", m->llmnr_hostname);

        return 0;
}

static int manager_sigusr1(sd_event_source *s, const struct signalfd_siginfo *si, void *userdata) {
        _cleanup_free_ char *buffer = NULL;
        _cleanup_fclose_ FILE *f = NULL;
        Manager *m = userdata;
        size_t size = 0;
        DnsScope *scope;

        assert(s);
        assert(si);
        assert(m);

        f = open_memstream(&buffer, &size);
        if (!f)
                return log_oom();

        LIST_FOREACH(scopes, scope, m->dns_scopes)
                dns_scope_dump(scope, f);

        if (fflush_and_check(f) < 0)
                return log_oom();

        log_dump(LOG_INFO, buffer);
        return 0;
}

int manager_new(Manager **ret) {
        _cleanup_(manager_freep) Manager *m = NULL;
        int r;

        assert(ret);

        m = new0(Manager, 1);
        if (!m)
                return -ENOMEM;

        m->llmnr_ipv4_udp_fd = m->llmnr_ipv6_udp_fd = -1;
        m->llmnr_ipv4_tcp_fd = m->llmnr_ipv6_tcp_fd = -1;
        m->mdns_ipv4_fd = m->mdns_ipv6_fd = -1;
        m->hostname_fd = -1;

        m->llmnr_support = RESOLVE_SUPPORT_YES;
        m->mdns_support = RESOLVE_SUPPORT_NO;
        m->dnssec_mode = DEFAULT_DNSSEC_MODE;
        m->read_resolv_conf = true;
        m->need_builtin_fallbacks = true;
        m->etc_hosts_last = m->etc_hosts_mtime = USEC_INFINITY;

        r = dns_trust_anchor_load(&m->trust_anchor);
        if (r < 0)
                return r;

        r = manager_parse_config_file(m);
        if (r < 0)
                return r;

        r = sd_event_default(&m->event);
        if (r < 0)
                return r;

        sd_event_add_signal(m->event, NULL, SIGTERM, NULL,  NULL);
        sd_event_add_signal(m->event, NULL, SIGINT, NULL, NULL);

        sd_event_set_watchdog(m->event, true);

        r = manager_watch_hostname(m);
        if (r < 0)
                return r;

        r = dns_scope_new(m, &m->unicast_scope, NULL, DNS_PROTOCOL_DNS, AF_UNSPEC);
        if (r < 0)
                return r;

        r = manager_network_monitor_listen(m);
        if (r < 0)
                return r;

        r = manager_rtnl_listen(m);
        if (r < 0)
                return r;

        r = manager_connect_bus(m);
        if (r < 0)
                return r;

        (void) sd_event_add_signal(m->event, &m->sigusr1_event_source, SIGUSR1, manager_sigusr1, m);

        *ret = m;
        m = NULL;

        return 0;
}

int manager_start(Manager *m) {
        int r;

        assert(m);

        r = manager_llmnr_start(m);
        if (r < 0)
                return r;

        r = manager_mdns_start(m);
        if (r < 0)
                return r;

        return 0;
}

Manager *manager_free(Manager *m) {
        Link *l;

        if (!m)
                return NULL;

        dns_server_unlink_all(m->dns_servers);
        dns_server_unlink_all(m->fallback_dns_servers);
        dns_search_domain_unlink_all(m->search_domains);

        while ((l = hashmap_first(m->links)))
               link_free(l);

        while (m->dns_queries)
                dns_query_free(m->dns_queries);

        dns_scope_free(m->unicast_scope);

        hashmap_free(m->links);
        hashmap_free(m->dns_transactions);

        sd_event_source_unref(m->network_event_source);
        sd_network_monitor_unref(m->network_monitor);

        sd_netlink_unref(m->rtnl);
        sd_event_source_unref(m->rtnl_event_source);

        manager_llmnr_stop(m);
        manager_mdns_stop(m);

        sd_bus_slot_unref(m->prepare_for_sleep_slot);
        sd_event_source_unref(m->bus_retry_event_source);
        sd_bus_unref(m->bus);

        sd_event_source_unref(m->sigusr1_event_source);

        sd_event_unref(m->event);

        dns_resource_key_unref(m->llmnr_host_ipv4_key);
        dns_resource_key_unref(m->llmnr_host_ipv6_key);

        sd_event_source_unref(m->hostname_event_source);
        safe_close(m->hostname_fd);
        free(m->llmnr_hostname);
        free(m->mdns_hostname);

        dns_trust_anchor_flush(&m->trust_anchor);
        manager_etc_hosts_flush(m);

        free(m);

        return NULL;
}

int manager_recv(Manager *m, int fd, DnsProtocol protocol, DnsPacket **ret) {
        _cleanup_(dns_packet_unrefp) DnsPacket *p = NULL;
        union {
                struct cmsghdr header; /* For alignment */
                uint8_t buffer[CMSG_SPACE(MAXSIZE(struct in_pktinfo, struct in6_pktinfo))
                               + CMSG_SPACE(int) /* ttl/hoplimit */
                               + EXTRA_CMSG_SPACE /* kernel appears to require extra buffer space */];
        } control;
        union sockaddr_union sa;
        struct msghdr mh = {};
        struct cmsghdr *cmsg;
        struct iovec iov;
        ssize_t ms, l;
        int r;

        assert(m);
        assert(fd >= 0);
        assert(ret);

        ms = next_datagram_size_fd(fd);
        if (ms < 0)
                return ms;

        r = dns_packet_new(&p, protocol, ms);
        if (r < 0)
                return r;

        iov.iov_base = DNS_PACKET_DATA(p);
        iov.iov_len = p->allocated;

        mh.msg_name = &sa.sa;
        mh.msg_namelen = sizeof(sa);
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = &control;
        mh.msg_controllen = sizeof(control);

        l = recvmsg(fd, &mh, 0);
        if (l < 0) {
                if (errno == EAGAIN || errno == EINTR)
                        return 0;

                return -errno;
        }

        if (l <= 0)
                return -EIO;

        assert(!(mh.msg_flags & MSG_CTRUNC));
        assert(!(mh.msg_flags & MSG_TRUNC));

        p->size = (size_t) l;

        p->family = sa.sa.sa_family;
        p->ipproto = IPPROTO_UDP;
        if (p->family == AF_INET) {
                p->sender.in = sa.in.sin_addr;
                p->sender_port = be16toh(sa.in.sin_port);
        } else if (p->family == AF_INET6) {
                p->sender.in6 = sa.in6.sin6_addr;
                p->sender_port = be16toh(sa.in6.sin6_port);
                p->ifindex = sa.in6.sin6_scope_id;
        } else
                return -EAFNOSUPPORT;

        CMSG_FOREACH(cmsg, &mh) {

                if (cmsg->cmsg_level == IPPROTO_IPV6) {
                        assert(p->family == AF_INET6);

                        switch (cmsg->cmsg_type) {

                        case IPV6_PKTINFO: {
                                struct in6_pktinfo *i = (struct in6_pktinfo*) CMSG_DATA(cmsg);

                                if (p->ifindex <= 0)
                                        p->ifindex = i->ipi6_ifindex;

                                p->destination.in6 = i->ipi6_addr;
                                break;
                        }

                        case IPV6_HOPLIMIT:
                                p->ttl = *(int *) CMSG_DATA(cmsg);
                                break;

                        }
                } else if (cmsg->cmsg_level == IPPROTO_IP) {
                        assert(p->family == AF_INET);

                        switch (cmsg->cmsg_type) {

                        case IP_PKTINFO: {
                                struct in_pktinfo *i = (struct in_pktinfo*) CMSG_DATA(cmsg);

                                if (p->ifindex <= 0)
                                        p->ifindex = i->ipi_ifindex;

                                p->destination.in = i->ipi_addr;
                                break;
                        }

                        case IP_TTL:
                                p->ttl = *(int *) CMSG_DATA(cmsg);
                                break;
                        }
                }
        }

        /* The Linux kernel sets the interface index to the loopback
         * device if the packet came from the local host since it
         * avoids the routing table in such a case. Let's unset the
         * interface index in such a case. */
        if (p->ifindex == LOOPBACK_IFINDEX)
                p->ifindex = 0;

        if (protocol != DNS_PROTOCOL_DNS) {
                /* If we don't know the interface index still, we look for the
                 * first local interface with a matching address. Yuck! */
                if (p->ifindex <= 0)
                        p->ifindex = manager_find_ifindex(m, p->family, &p->destination);
        }

        *ret = p;
        p = NULL;

        return 1;
}

static int sendmsg_loop(int fd, struct msghdr *mh, int flags) {
        int r;

        assert(fd >= 0);
        assert(mh);

        for (;;) {
                if (sendmsg(fd, mh, flags) >= 0)
                        return 0;

                if (errno == EINTR)
                        continue;

                if (errno != EAGAIN)
                        return -errno;

                r = fd_wait_for_event(fd, POLLOUT, SEND_TIMEOUT_USEC);
                if (r < 0)
                        return r;
                if (r == 0)
                        return -ETIMEDOUT;
        }
}

static int write_loop(int fd, void *message, size_t length) {
        int r;

        assert(fd >= 0);
        assert(message);

        for (;;) {
                if (write(fd, message, length) >= 0)
                        return 0;

                if (errno == EINTR)
                        continue;

                if (errno != EAGAIN)
                        return -errno;

                r = fd_wait_for_event(fd, POLLOUT, SEND_TIMEOUT_USEC);
                if (r < 0)
                        return r;
                if (r == 0)
                        return -ETIMEDOUT;
        }
}

int manager_write(Manager *m, int fd, DnsPacket *p) {
        int r;

        log_debug("Sending %s packet with id %" PRIu16 ".", DNS_PACKET_QR(p) ? "response" : "query", DNS_PACKET_ID(p));

        r = write_loop(fd, DNS_PACKET_DATA(p), p->size);
        if (r < 0)
                return r;

        return 0;
}

static int manager_ipv4_send(Manager *m, int fd, int ifindex, const struct in_addr *addr, uint16_t port, DnsPacket *p) {
        union sockaddr_union sa = {
                .in.sin_family = AF_INET,
        };
        union {
                struct cmsghdr header; /* For alignment */
                uint8_t buffer[CMSG_SPACE(sizeof(struct in_pktinfo))];
        } control;
        struct msghdr mh = {};
        struct iovec iov;

        assert(m);
        assert(fd >= 0);
        assert(addr);
        assert(port > 0);
        assert(p);

        iov.iov_base = DNS_PACKET_DATA(p);
        iov.iov_len = p->size;

        sa.in.sin_addr = *addr;
        sa.in.sin_port = htobe16(port),

        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_name = &sa.sa;
        mh.msg_namelen = sizeof(sa.in);

        if (ifindex > 0) {
                struct cmsghdr *cmsg;
                struct in_pktinfo *pi;

                zero(control);

                mh.msg_control = &control;
                mh.msg_controllen = CMSG_LEN(sizeof(struct in_pktinfo));

                cmsg = CMSG_FIRSTHDR(&mh);
                cmsg->cmsg_len = mh.msg_controllen;
                cmsg->cmsg_level = IPPROTO_IP;
                cmsg->cmsg_type = IP_PKTINFO;

                pi = (struct in_pktinfo*) CMSG_DATA(cmsg);
                pi->ipi_ifindex = ifindex;
        }

        return sendmsg_loop(fd, &mh, 0);
}

static int manager_ipv6_send(Manager *m, int fd, int ifindex, const struct in6_addr *addr, uint16_t port, DnsPacket *p) {
        union sockaddr_union sa = {
                .in6.sin6_family = AF_INET6,
        };
        union {
                struct cmsghdr header; /* For alignment */
                uint8_t buffer[CMSG_SPACE(sizeof(struct in6_pktinfo))];
        } control;
        struct msghdr mh = {};
        struct iovec iov;

        assert(m);
        assert(fd >= 0);
        assert(addr);
        assert(port > 0);
        assert(p);

        iov.iov_base = DNS_PACKET_DATA(p);
        iov.iov_len = p->size;

        sa.in6.sin6_addr = *addr;
        sa.in6.sin6_port = htobe16(port),
        sa.in6.sin6_scope_id = ifindex;

        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_name = &sa.sa;
        mh.msg_namelen = sizeof(sa.in6);

        if (ifindex > 0) {
                struct cmsghdr *cmsg;
                struct in6_pktinfo *pi;

                zero(control);

                mh.msg_control = &control;
                mh.msg_controllen = CMSG_LEN(sizeof(struct in6_pktinfo));

                cmsg = CMSG_FIRSTHDR(&mh);
                cmsg->cmsg_len = mh.msg_controllen;
                cmsg->cmsg_level = IPPROTO_IPV6;
                cmsg->cmsg_type = IPV6_PKTINFO;

                pi = (struct in6_pktinfo*) CMSG_DATA(cmsg);
                pi->ipi6_ifindex = ifindex;
        }

        return sendmsg_loop(fd, &mh, 0);
}

int manager_send(Manager *m, int fd, int ifindex, int family, const union in_addr_union *addr, uint16_t port, DnsPacket *p) {
        assert(m);
        assert(fd >= 0);
        assert(addr);
        assert(port > 0);
        assert(p);

        log_debug("Sending %s packet with id %" PRIu16 " on interface %i/%s.", DNS_PACKET_QR(p) ? "response" : "query", DNS_PACKET_ID(p), ifindex, af_to_name(family));

        if (family == AF_INET)
                return manager_ipv4_send(m, fd, ifindex, &addr->in, port, p);
        else if (family == AF_INET6)
                return manager_ipv6_send(m, fd, ifindex, &addr->in6, port, p);

        return -EAFNOSUPPORT;
}

uint32_t manager_find_mtu(Manager *m) {
        uint32_t mtu = 0;
        Link *l;
        Iterator i;

        /* If we don't know on which link a DNS packet would be
         * delivered, let's find the largest MTU that works on all
         * interfaces we know of */

        HASHMAP_FOREACH(l, m->links, i) {
                if (l->mtu <= 0)
                        continue;

                if (mtu <= 0 || l->mtu < mtu)
                        mtu = l->mtu;
        }

        return mtu;
}

int manager_find_ifindex(Manager *m, int family, const union in_addr_union *in_addr) {
        LinkAddress *a;

        assert(m);

        a = manager_find_link_address(m, family, in_addr);
        if (a)
                return a->link->ifindex;

        return 0;
}

void manager_refresh_rrs(Manager *m) {
        Iterator i;
        Link *l;

        assert(m);

        m->llmnr_host_ipv4_key = dns_resource_key_unref(m->llmnr_host_ipv4_key);
        m->llmnr_host_ipv6_key = dns_resource_key_unref(m->llmnr_host_ipv6_key);

        HASHMAP_FOREACH(l, m->links, i) {
                link_add_rrs(l, true);
                link_add_rrs(l, false);
        }
}

int manager_next_hostname(Manager *m) {
        const char *p;
        uint64_t u, a;
        char *h, *k;
        int r;

        assert(m);

        p = strchr(m->llmnr_hostname, 0);
        assert(p);

        while (p > m->llmnr_hostname) {
                if (!strchr("0123456789", p[-1]))
                        break;

                p--;
        }

        if (*p == 0 || safe_atou64(p, &u) < 0 || u <= 0)
                u = 1;

        /* Add a random number to the old value. This way we can avoid
         * that two hosts pick the same hostname, win on IPv4 and lose
         * on IPv6 (or vice versa), and pick the same hostname
         * replacement hostname, ad infinitum. We still want the
         * numbers to go up monotonically, hence we just add a random
         * value 1..10 */

        random_bytes(&a, sizeof(a));
        u += 1 + a % 10;

        if (asprintf(&h, "%.*s%" PRIu64, (int) (p - m->llmnr_hostname), m->llmnr_hostname, u) < 0)
                return -ENOMEM;

        r = dns_name_concat(h, "local", &k);
        if (r < 0) {
                free(h);
                return r;
        }

        log_info("Hostname conflict, changing published hostname from '%s' to '%s'.", m->llmnr_hostname, h);

        free(m->llmnr_hostname);
        m->llmnr_hostname = h;

        free(m->mdns_hostname);
        m->mdns_hostname = k;

        manager_refresh_rrs(m);

        return 0;
}

LinkAddress* manager_find_link_address(Manager *m, int family, const union in_addr_union *in_addr) {
        Iterator i;
        Link *l;

        assert(m);

        HASHMAP_FOREACH(l, m->links, i) {
                LinkAddress *a;

                a = link_find_address(l, family, in_addr);
                if (a)
                        return a;
        }

        return NULL;
}

bool manager_our_packet(Manager *m, DnsPacket *p) {
        assert(m);
        assert(p);

        return !!manager_find_link_address(m, p->family, &p->sender);
}

DnsScope* manager_find_scope(Manager *m, DnsPacket *p) {
        Link *l;

        assert(m);
        assert(p);

        l = hashmap_get(m->links, INT_TO_PTR(p->ifindex));
        if (!l)
                return NULL;

        switch (p->protocol) {
        case DNS_PROTOCOL_LLMNR:
                if (p->family == AF_INET)
                        return l->llmnr_ipv4_scope;
                else if (p->family == AF_INET6)
                        return l->llmnr_ipv6_scope;

                break;

        case DNS_PROTOCOL_MDNS:
                if (p->family == AF_INET)
                        return l->mdns_ipv4_scope;
                else if (p->family == AF_INET6)
                        return l->mdns_ipv6_scope;

                break;

        default:
                break;
        }

        return NULL;
}

void manager_verify_all(Manager *m) {
        DnsScope *s;

        assert(m);

        LIST_FOREACH(scopes, s, m->dns_scopes)
                dns_zone_verify_all(&s->zone);
}

int manager_is_own_hostname(Manager *m, const char *name) {
        int r;

        assert(m);
        assert(name);

        if (m->llmnr_hostname) {
                r = dns_name_equal(name, m->llmnr_hostname);
                if (r != 0)
                        return r;
        }

        if (m->mdns_hostname)
                return dns_name_equal(name, m->mdns_hostname);

        return 0;
}

int manager_compile_dns_servers(Manager *m, OrderedSet **dns) {
        DnsServer *s;
        Iterator i;
        Link *l;
        int r;

        assert(m);
        assert(dns);

        r = ordered_set_ensure_allocated(dns, &dns_server_hash_ops);
        if (r < 0)
                return r;

        /* First add the system-wide servers and domains */
        LIST_FOREACH(servers, s, m->dns_servers) {
                r = ordered_set_put(*dns, s);
                if (r == -EEXIST)
                        continue;
                if (r < 0)
                        return r;
        }

        /* Then, add the per-link servers */
        HASHMAP_FOREACH(l, m->links, i) {
                LIST_FOREACH(servers, s, l->dns_servers) {
                        r = ordered_set_put(*dns, s);
                        if (r == -EEXIST)
                                continue;
                        if (r < 0)
                                return r;
                }
        }

        /* If we found nothing, add the fallback servers */
        if (ordered_set_isempty(*dns)) {
                LIST_FOREACH(servers, s, m->fallback_dns_servers) {
                        r = ordered_set_put(*dns, s);
                        if (r == -EEXIST)
                                continue;
                        if (r < 0)
                                return r;
                }
        }

        return 0;
}

int manager_compile_search_domains(Manager *m, OrderedSet **domains) {
        DnsSearchDomain *d;
        Iterator i;
        Link *l;
        int r;

        assert(m);
        assert(domains);

        r = ordered_set_ensure_allocated(domains, &dns_name_hash_ops);
        if (r < 0)
                return r;

        LIST_FOREACH(domains, d, m->search_domains) {
                r = ordered_set_put(*domains, d->name);
                if (r == -EEXIST)
                        continue;
                if (r < 0)
                        return r;
        }

        HASHMAP_FOREACH(l, m->links, i) {

                LIST_FOREACH(domains, d, l->search_domains) {
                        r = ordered_set_put(*domains, d->name);
                        if (r == -EEXIST)
                                continue;
                        if (r < 0)
                                return r;
                }
        }

        return 0;
}

DnssecMode manager_get_dnssec_mode(Manager *m) {
        assert(m);

        if (m->dnssec_mode != _DNSSEC_MODE_INVALID)
                return m->dnssec_mode;

        return DNSSEC_NO;
}

bool manager_dnssec_supported(Manager *m) {
        DnsServer *server;
        Iterator i;
        Link *l;

        assert(m);

        if (manager_get_dnssec_mode(m) == DNSSEC_NO)
                return false;

        server = manager_get_dns_server(m);
        if (server && !dns_server_dnssec_supported(server))
                return false;

        HASHMAP_FOREACH(l, m->links, i)
                if (!link_dnssec_supported(l))
                        return false;

        return true;
}

void manager_dnssec_verdict(Manager *m, DnssecVerdict verdict, const DnsResourceKey *key) {

        assert(verdict >= 0);
        assert(verdict < _DNSSEC_VERDICT_MAX);

        if (log_get_max_level() >= LOG_DEBUG) {
                char s[DNS_RESOURCE_KEY_STRING_MAX];

                log_debug("Found verdict for lookup %s: %s",
                          dns_resource_key_to_string(key, s, sizeof s),
                          dnssec_verdict_to_string(verdict));
        }

        m->n_dnssec_verdict[verdict]++;
}

bool manager_routable(Manager *m, int family) {
        Iterator i;
        Link *l;

        assert(m);

        /* Returns true if the host has at least one interface with a routable address of the specified type */

        HASHMAP_FOREACH(l, m->links, i)
                if (link_relevant(l, family, false))
                        return true;

        return false;
}
