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

#include "systemd-shared/dns-domain.h"

typedef enum DnssecResult DnssecResult;
typedef enum DnssecVerdict DnssecVerdict;

#include "resolved-dns-answer.h"
#include "resolved-dns-rr.h"

enum DnssecResult {
        /* These five are returned by dnssec_verify_rrset() */
        DNSSEC_VALIDATED,
        DNSSEC_VALIDATED_WILDCARD, /* Validated via a wildcard RRSIG, further NSEC/NSEC3 checks necessary */
        DNSSEC_INVALID,
        DNSSEC_SIGNATURE_EXPIRED,
        DNSSEC_UNSUPPORTED_ALGORITHM,

        /* These two are added by dnssec_verify_rrset_search() */
        DNSSEC_NO_SIGNATURE,
        DNSSEC_MISSING_KEY,

        /* These two are added by the DnsTransaction logic */
        DNSSEC_UNSIGNED,
        DNSSEC_FAILED_AUXILIARY,
        DNSSEC_NSEC_MISMATCH,
        DNSSEC_INCOMPATIBLE_SERVER,

        _DNSSEC_RESULT_MAX,
        _DNSSEC_RESULT_INVALID = -1
};

enum DnssecVerdict {
        DNSSEC_SECURE,
        DNSSEC_INSECURE,
        DNSSEC_BOGUS,
        DNSSEC_INDETERMINATE,

        _DNSSEC_VERDICT_MAX,
        _DNSSEC_VERDICT_INVALID = -1
};

#define DNSSEC_CANONICAL_HOSTNAME_MAX (DNS_HOSTNAME_MAX + 2)

/* The longest digest we'll ever generate, of all digest algorithms we support */
#define DNSSEC_HASH_SIZE_MAX (MAX(20, 32))

int dnssec_rrsig_match_dnskey(DnsResourceRecord *rrsig, DnsResourceRecord *dnskey, bool revoked_ok);
int dnssec_key_match_rrsig(const DnsResourceKey *key, DnsResourceRecord *rrsig);

int dnssec_verify_rrset(DnsAnswer *answer, const DnsResourceKey *key, DnsResourceRecord *rrsig, DnsResourceRecord *dnskey, usec_t realtime, DnssecResult *result);
int dnssec_verify_rrset_search(DnsAnswer *answer, const DnsResourceKey *key, DnsAnswer *validated_dnskeys, usec_t realtime, DnssecResult *result, DnsResourceRecord **rrsig);

int dnssec_verify_dnskey_by_ds(DnsResourceRecord *dnskey, DnsResourceRecord *ds, bool mask_revoke);
int dnssec_verify_dnskey_by_ds_search(DnsResourceRecord *dnskey, DnsAnswer *validated_ds);

int dnssec_has_rrsig(DnsAnswer *a, const DnsResourceKey *key);

uint16_t dnssec_keytag(DnsResourceRecord *dnskey, bool mask_revoke);

int dnssec_canonicalize(const char *n, char *buffer, size_t buffer_max);

int dnssec_nsec3_hash(DnsResourceRecord *nsec3, const char *name, void *ret);

typedef enum DnssecNsecResult {
        DNSSEC_NSEC_NO_RR,     /* No suitable NSEC/NSEC3 RR found */
        DNSSEC_NSEC_CNAME,     /* Didn't find what was asked for, but did find CNAME */
        DNSSEC_NSEC_UNSUPPORTED_ALGORITHM,
        DNSSEC_NSEC_NXDOMAIN,
        DNSSEC_NSEC_NODATA,
        DNSSEC_NSEC_FOUND,
        DNSSEC_NSEC_OPTOUT,
} DnssecNsecResult;

int dnssec_nsec_test(DnsAnswer *answer, DnsResourceKey *key, DnssecNsecResult *result, bool *authenticated, uint32_t *ttl);


int dnssec_test_positive_wildcard(DnsAnswer *a, const char *name, const char *source, const char *zone, bool *authenticated);

const char* dnssec_result_to_string(DnssecResult m) _const_;
DnssecResult dnssec_result_from_string(const char *s) _pure_;

const char* dnssec_verdict_to_string(DnssecVerdict m) _const_;
DnssecVerdict dnssec_verdict_from_string(const char *s) _pure_;
