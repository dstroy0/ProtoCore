// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_ntrip_caster_listener.h
 * @brief Server-side NTRIP caster listener (PC_ENABLE_NTRIP_CASTER): the ConnProto::PROTO_NTRIP_CASTER
 *        handler that answers rover requests and streams RTCM to subscribers.
 *
 * The pure codec (pc_ntrip_caster.h) parses requests and builds responses / the source table; this file owns
 * the connection state: it reads each rover's request off its socket, replies (stream-accept, source
 * table, error, or 401), and then fans RTCM correction bytes out to every rover subscribed to a mountpoint.
 * Layered exactly like services/net/relay - the app opens the listener, adds one or more mountpoints, then
 * pushes RTCM as its survey/receiver produces it:
 *
 * @code
 *   int32_t li = server.listen(2101, ConnProto::PROTO_NTRIP_CASTER);   // 2101 = the IANA NTRIP port
 *   NtripMount m = {};
 *   m.mountpoint = "BASE1";
 *   m.identifier = "Lab roof";
 *   m.format_details = "1005(1)";
 *   m.lat_deg = 37.77; m.lon_deg = -122.42;
 *   pc_ntrip_caster_add_mount((uint8_t)li, &m, nullptr);              // null = open (no auth)
 *   ...
 *   uint8_t frame[64];
 *   size_t n = pc_rtcm3_build_1005(frame, sizeof(frame), 2003, x01mm, y01mm, z01mm);
 *   pc_ntrip_caster_broadcast("BASE1", frame, n);                    // -> every subscribed rover
 * @endcode
 *
 * The @c NtripMount's string fields must remain valid for the caster's lifetime (they are referenced, not
 * copied - configure them from static storage). Only the mountpoint name and optional credentials are
 * copied internally.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NTRIP_LISTENER_H
#define PROTOCORE_NTRIP_LISTENER_H

#include "protocore_config.h"

#if PC_ENABLE_NTRIP_CASTER

#include "services/timing_position/gnss/ntrip_caster.h"

/**
 * @brief Register a mountpoint the caster serves and install the handler (first call).
 *
 * @param listener_id  the id from `server.listen(port, ConnProto::PROTO_NTRIP_CASTER)`.
 * @param mount        the source-table description; string fields are referenced (keep them alive).
 * @param auth_b64     optional base64 of "user:pass" a rover must present via HTTP Basic, or null for open
 *                     access. Referenced, not copied.
 * @return true; false if @p mount / its mountpoint is null or too long, or the mount table is full.
 */
proto_bool pc_ntrip_caster_add_mount(uint8_t listener_id, const NtripMount *mount, const char *auth_b64);

/**
 * @brief Push RTCM bytes to every rover currently streaming @p mountpoint.
 * @return the number of rovers the bytes were queued to.
 */
int pc_ntrip_caster_broadcast(const char *mountpoint, const uint8_t *data, size_t len);

/** @brief Number of rovers currently streaming @p mountpoint (observability). */
int pc_ntrip_caster_subscriber_count(const char *mountpoint);

/** @brief Clear all mounts and drop all rover state (start from empty). */
void pc_ntrip_caster_reset(void);

#endif // PC_ENABLE_NTRIP_CASTER

#endif // PROTOCORE_NTRIP_LISTENER_H
