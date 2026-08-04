// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_server.h
 * @brief Authoritative DNS server (UDP/53) - resolve local names on an offline LAN.
 *
 * A tiny name server for a network with no path to a real DNS: register `name -> IPv4` A
 * records with pc_dns_server_add() and the device answers matching A/IN queries from that fixed
 * table (NXDOMAIN for anything else). Devices can then use `printer.lan` instead of
 * `192.168.1.5`, a companion to the NTP server for self-hosted, offline infrastructure. Zero
 * heap; gated by PC_ENABLE_DNS_SERVER.
 *
 * The response builder (pc_dns_server_build_response) is pure - it parses the query and, via a
 * resolver callback, writes the reply - so the wire format is host-tested with no lwIP.
 * pc_dns_server_begin() binds UDP/53 through the transport UDP service and serves the built-in
 * table (pc_dns_server_lookup). It is a general resolver, distinct from the provisioning
 * captive-portal DNS (which points every name at the softAP); do not enable both.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNS_SERVER_H
#define PROTOCORE_DNS_SERVER_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

PROTO_BEGIN_DECLS

#if PC_ENABLE_DNS_SERVER

/**
 * @brief Resolve a queried name to an IPv4 address.
 * @param name the queried name, lower/upper case as sent (match case-insensitively).
 * @return the address in host byte order (0xC0A80105 == 192.168.1.5), or 0 for "not found".
 */
typedef uint32_t (*DnsResolveFn)(const char *name);

/**
 * @brief Build a DNS reply to @p query. Pure - no clock, no I/O.
 *
 * Parses the first question; for an A/IN query it looks the name up via @p resolve and, on a
 * hit, appends a single A answer (compressed name pointer, @p ttl, the address). A miss on an
 * A query returns NXDOMAIN; a non-A query returns no answer with RCODE 0. The query id and the
 * recursion-desired bit are preserved and AA (authoritative) is set.
 *
 * @param query    the received query bytes.
 * @param qlen     length of @p query (must be >= 12, the DNS header).
 * @param ttl      TTL (seconds) to advertise on the answer.
 * @param resolve  the name -> IPv4 resolver.
 * @param out      output buffer.
 * @param out_cap  capacity of @p out.
 * @return         response length, or 0 on a malformed query or insufficient capacity.
 */
size_t pc_dns_server_build_response(const uint8_t *query, size_t qlen, uint32_t ttl, DnsResolveFn resolve, uint8_t *out,
                                    size_t out_cap);

/**
 * @brief Add an A record to the built-in table (case-insensitive name).
 * @return true if stored, false if the name is invalid or the table is full.
 */
proto_bool pc_dns_server_add(const char *name, uint8_t a, uint8_t b, uint8_t c, uint8_t d);

/** @brief Look @p name up in the built-in table. @return host-order IPv4, or 0 if absent. */
uint32_t pc_dns_server_lookup(const char *name);

/** @brief Remove every record from the built-in table. */
void pc_dns_server_clear();

/**
 * @brief Start answering DNS queries on UDP/53 from the built-in table.
 * @return true if the UDP listener bound; false on a host build or if the port is taken.
 */
proto_bool pc_dns_server_begin();

#endif // PC_ENABLE_DNS_SERVER

PROTO_END_DECLS

#endif // PROTOCORE_DNS_SERVER_H
