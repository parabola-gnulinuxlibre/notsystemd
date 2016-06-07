/***
  This file is part of systemd.

  Copyright (C) 2014 Tom Gundersen
  Copyright (C) 2014 Susant Sahani

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

#include <systemd/sd-lldp.h>

#include "alloc-util.h"
#include "lldp-internal.h"

/* We store maximum 1K chassis entries */
#define LLDP_MIB_MAX_CHASSIS 1024

/* Maximum Ports can be attached to any chassis */
#define LLDP_MIB_MAX_PORT_PER_CHASSIS 32

/* 10.5.5.2.2 mibUpdateObjects ()
 * The mibUpdateObjects () procedure updates the MIB objects corresponding to
 * the TLVs contained in the received LLDPDU for the LLDP remote system
 * indicated by the LLDP remote systems update process defined in 10.3.5 */

int lldp_mib_update_objects(lldp_chassis *c, tlv_packet *tlv) {
        lldp_neighbour_port *p;
        uint16_t length, ttl;
        uint8_t *data;
        uint8_t type;
        int r;

        assert_return(c, -EINVAL);
        assert_return(tlv, -EINVAL);

        r = sd_lldp_packet_read_port_id(tlv, &type, &data, &length);
        if (r < 0)
                return r;

        /* Update the packet if we already have */
        LIST_FOREACH(port, p, c->ports) {

                if ((p->type == type && p->length == length && !memcmp(p->data, data, p->length))) {

                        r = sd_lldp_packet_read_ttl(tlv, &ttl);
                        if (r < 0)
                                return r;

                        p->until = ttl * USEC_PER_SEC + now(clock_boottime_or_monotonic());

                        sd_lldp_packet_unref(p->packet);
                        p->packet = tlv;

                        prioq_reshuffle(p->c->by_expiry, p, &p->prioq_idx);

                        return 0;
                }
        }

        return -1;
}

int lldp_mib_remove_objects(lldp_chassis *c, tlv_packet *tlv) {
        lldp_neighbour_port *p, *q;
        uint8_t *data;
        uint16_t length;
        uint8_t type;
        int r;

        assert_return(c, -EINVAL);
        assert_return(tlv, -EINVAL);

        r = sd_lldp_packet_read_port_id(tlv, &type, &data, &length);
        if (r < 0)
                return r;

        LIST_FOREACH_SAFE(port, p, q, c->ports) {

                /* Find the port */
                if (p->type == type && p->length == length && !memcmp(p->data, data, p->length)) {
                        lldp_neighbour_port_remove_and_free(p);
                        break;
                }
        }

        return 0;
}

int lldp_mib_add_objects(Prioq *by_expiry,
                         Hashmap *neighbour_mib,
                         tlv_packet *tlv) {
        _cleanup_lldp_neighbour_port_free_ lldp_neighbour_port *p = NULL;
        _cleanup_lldp_chassis_free_ lldp_chassis *c = NULL;
        lldp_chassis_id chassis_id;
        bool new_chassis = false;
        uint8_t subtype, *data;
        uint16_t ttl, length;
        int r;

        assert_return(by_expiry, -EINVAL);
        assert_return(neighbour_mib, -EINVAL);
        assert_return(tlv, -EINVAL);

        r = sd_lldp_packet_read_chassis_id(tlv, &subtype, &data, &length);
        if (r < 0)
                goto drop;

        r = sd_lldp_packet_read_ttl(tlv, &ttl);
        if (r < 0)
                goto drop;

        /* Make hash key */
        chassis_id.type = subtype;
        chassis_id.length = length;
        chassis_id.data = data;

        /* Try to find the Chassis */
        c = hashmap_get(neighbour_mib, &chassis_id);
        if (!c) {

                /* Don't create chassis if ttl 0 is received . Silently drop it */
                if (ttl == 0) {
                        log_lldp("TTL value 0 received. Skiping Chassis creation.");
                        goto drop;
                }

                /* Admission Control: Can we store this packet ? */
                if (hashmap_size(neighbour_mib) >= LLDP_MIB_MAX_CHASSIS) {

                        log_lldp("Exceeding number of chassie: %d. Dropping ...",
                                 hashmap_size(neighbour_mib));
                        goto drop;
                }

                r = lldp_chassis_new(tlv, by_expiry, neighbour_mib, &c);
                if (r < 0)
                        goto drop;

                new_chassis = true;

                r = hashmap_put(neighbour_mib, &c->chassis_id, c);
                if (r < 0)
                        goto drop;

        } else {

                /* When the TTL field is set to zero, the receiving LLDP agent is notified all
                 * system information associated with the LLDP agent/port is to be deleted */
                if (ttl == 0) {
                        log_lldp("TTL value 0 received . Deleting associated Port ...");

                        lldp_mib_remove_objects(c, tlv);

                        c = NULL;
                        goto drop;
                }

                /* if we already have this port just update it */
                r = lldp_mib_update_objects(c, tlv);
                if (r >= 0) {
                        c = NULL;
                        return r;
                }

                /* Admission Control: Can this port attached to the existing chassis ? */
                if (c->n_ref >= LLDP_MIB_MAX_PORT_PER_CHASSIS) {
                        log_lldp("Port limit reached. Chassis has: %d ports. Dropping ...", c->n_ref);

                        c = NULL;
                        goto drop;
                }
        }

        /* This is a new port */
        r = lldp_neighbour_port_new(c, tlv, &p);
        if (r < 0)
                goto drop;

        r = prioq_put(c->by_expiry, p, &p->prioq_idx);
        if (r < 0)
                goto drop;

        /* Attach new port to chassis */
        LIST_PREPEND(port, c->ports, p);
        c->n_ref ++;

        p = NULL;
        c = NULL;

        return 0;

 drop:
        sd_lldp_packet_unref(tlv);

        if (new_chassis)
                hashmap_remove(neighbour_mib, &c->chassis_id);

        return r;
}

void lldp_neighbour_port_remove_and_free(lldp_neighbour_port *p) {
        lldp_chassis *c;

        assert(p);
        assert(p->c);

        c = p->c;

        prioq_remove(c->by_expiry, p, &p->prioq_idx);

        LIST_REMOVE(port, c->ports, p);
        lldp_neighbour_port_free(p);

        /* Drop the Chassis if no port is attached  */
        c->n_ref --;
        if (c->n_ref <= 1) {
                hashmap_remove(c->neighbour_mib, &c->chassis_id);
                lldp_chassis_free(c);
        }
}

void lldp_neighbour_port_free(lldp_neighbour_port *p) {

        if(!p)
                return;

        sd_lldp_packet_unref(p->packet);

        free(p->data);
        free(p);
}

int lldp_neighbour_port_new(lldp_chassis *c,
                            tlv_packet *tlv,
                            lldp_neighbour_port **ret) {
        _cleanup_lldp_neighbour_port_free_ lldp_neighbour_port *p = NULL;
        uint16_t length, ttl;
        uint8_t *data;
        uint8_t type;
        int r;

        assert(tlv);

        r = sd_lldp_packet_read_port_id(tlv, &type, &data, &length);
        if (r < 0)
                return r;

        r = sd_lldp_packet_read_ttl(tlv, &ttl);
        if (r < 0)
                return r;

        p = new0(lldp_neighbour_port, 1);
        if (!p)
                return -ENOMEM;

        p->c = c;
        p->type = type;
        p->length = length;
        p->packet = tlv;
        p->prioq_idx = PRIOQ_IDX_NULL;
        p->until = ttl * USEC_PER_SEC + now(clock_boottime_or_monotonic());

        p->data = memdup(data, length);
        if (!p->data)
                return -ENOMEM;

        *ret = p;
        p = NULL;

        return 0;
}

void lldp_chassis_free(lldp_chassis *c) {

        if (!c)
                return;

        if (c->n_ref > 1)
                return;

        free(c->chassis_id.data);
        free(c);
}

int lldp_chassis_new(tlv_packet *tlv,
                     Prioq *by_expiry,
                     Hashmap *neighbour_mib,
                     lldp_chassis **ret) {
        _cleanup_lldp_chassis_free_ lldp_chassis *c = NULL;
        uint16_t length;
        uint8_t *data;
        uint8_t type;
        int r;

        assert(tlv);

        r = sd_lldp_packet_read_chassis_id(tlv, &type, &data, &length);
        if (r < 0)
                return r;

        c = new0(lldp_chassis, 1);
        if (!c)
                return -ENOMEM;

        c->n_ref = 1;
        c->chassis_id.type = type;
        c->chassis_id.length = length;

        c->chassis_id.data = memdup(data, length);
        if (!c->chassis_id.data)
                return -ENOMEM;

        LIST_HEAD_INIT(c->ports);

        c->by_expiry = by_expiry;
        c->neighbour_mib = neighbour_mib;

        *ret = c;
        c = NULL;

        return 0;
}

int lldp_receive_packet(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
        _cleanup_(sd_lldp_packet_unrefp) tlv_packet *packet = NULL;
        tlv_packet *p;
        uint16_t length;
        int r;

        assert(fd);
        assert(userdata);

        r = tlv_packet_new(&packet);
        if (r < 0)
                return r;

        length = read(fd, &packet->pdu, sizeof(packet->pdu));

        /* Silently drop the packet */
        if ((size_t) length > ETHER_MAX_LEN)
                return 0;

        packet->userdata = userdata;

        p = packet;
        packet = NULL;

        return lldp_handle_packet(p, (uint16_t) length);
}
