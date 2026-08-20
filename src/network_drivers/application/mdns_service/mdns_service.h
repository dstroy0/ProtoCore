// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mdns_service.h
 * @brief Optional mDNS / DNS-SD advertisement (PROTOCORE_ENABLE_MDNS).
 *
 * Responds for `<hostname>.local` and advertises an `_http._tcp` service, with optional TXT records
 * and extra service types.
 *
 * Two backends, chosen by PROTOCORE_HAS_VENDOR_MDNS. Where the SDK ships a responder the wrapper drives
 * that one, because it also probes and resolves name conflicts. Where it does not, the portable
 * responder answers RFC 6762 / RFC 6763 queries on 224.0.0.251:5353 over the UDP listener: A for the
 * host, PTR for the enumeration name and each service type, and SRV + TXT per instance. It advertises
 * rather than defends - no probing, no conflict resolution - so two devices given the same hostname
 * on one link both answer to it.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MDNS_SERVICE_H
#define PROTOCORE_MDNS_SERVICE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_MDNS

PROTOCORE_BEGIN_DECLS

// PROTOCORE_MDNS_SERVICE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief What begin takes: hostname, http_port. */
typedef struct
{
    const char *hostname; ///< Host label without the `.local` suffix (e.g. "mydevice")
    uint16_t http_port;   ///< TCP port the HTTP server listens on (default 80)
} MdnsServiceBeginArgs;

/** @brief What txt takes: key, value. */
typedef struct
{
    const char *key;
    const char *value;
} MdnsServiceTxtArgs;

/** @brief What add_service takes: service_type, proto, port. */
typedef struct
{
    const char *service_type; ///< DNS-SD service type, e.g. `"_https"`
    const char *proto;        ///< `"_tcp"` or `"_udp"`
    uint16_t port;            ///< TCP/UDP port the service listens on
} MdnsServiceAddServiceArgs;

/**
 * @brief Optional mDNS / DNS-SD advertisement (PROTOCORE_ENABLE_MDNS).
 *
 * A caller sets the members a call takes, invokes it through ::MdnsService with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   MdnsService.begin_args.hostname = ...;
 *   MdnsService.begin_args.http_port = ...;
 *   MdnsService.begin(work);
 *   // MdnsService.ok is what the call reports
 *
 * @var MdnsServiceNs::begin_args  what begin takes: hostname, http_port
 * @var MdnsServiceNs::txt_args  what txt takes: key, value
 * @var MdnsServiceNs::add_service_args  what add_service takes: service_type, proto, port
 * @var MdnsServiceNs::ok  true if the responder started; false if disabled at compile time, ...
 * @var MdnsServiceNs::begin  start mDNS responder and advertise an HTTP service. Call once after ...
 * @var MdnsServiceNs::txt  add a TXT key/value record to the advertised `_http._tcp` service. ...
 * @var MdnsServiceNs::add_service  advertise an additional service, e.g. `("_https", "_tcp", 443)`
 *
 * @c work is PROTOCORE_MDNS_SERVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    MdnsServiceBeginArgs begin_args;
    MdnsServiceTxtArgs txt_args;
    MdnsServiceAddServiceArgs add_service_args;
    proto_bool ok;
} MdnsServiceVars;

/** @brief The operands and the outcome. */
extern MdnsServiceVars MdnsServiceV;

/** @brief The entries. */
typedef struct
{
    void (*const begin)(uint8_t *restrict work);
    void (*const txt)(uint8_t *restrict work);
    void (*const add_service)(uint8_t *restrict work);
} MdnsServiceNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in MdnsServiceV or a region of the borrow at a fixed offset.
void protocore_mdns_service_begin(uint8_t *restrict work);
void protocore_mdns_service_txt(uint8_t *restrict work);
void protocore_mdns_service_add_service(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `MdnsService.begin(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const MdnsServiceNs MdnsService __attribute__((unused)) = {
    .begin = protocore_mdns_service_begin,
    .txt = protocore_mdns_service_txt,
    .add_service = protocore_mdns_service_add_service,
};

/**
 * @brief The PROTOCORE_MDNS_SERVICE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_mdns_service_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MDNS

#endif // PROTOCORE_MDNS_SERVICE_H
