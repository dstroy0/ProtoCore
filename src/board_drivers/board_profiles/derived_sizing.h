// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file derived_sizing.h
 * @brief Feature-driven sizing resolution - the per-variant sizing layer's final pass.
 *
 * Some buffer sizes have a hard lower bound set by which features are compiled in, not by the
 * board. A ring smaller than that bound does not just waste a little RAM - it breaks the feature
 * (a handshake resets, a streamed upload deadlocks). That coupling is sizing logic, so it lives
 * here in the board-profile layer rather than inline in protocore_config.h, and it runs LAST: after
 * the chip/PSRAM/flash profiles, the base `#ifndef` defaults, and every `PC_ENABLE_*` feature
 * flag are resolved, so it can see the final values.
 *
 * The floor is enforced against whatever set the value - a chip profile, a `-D` build flag, or the
 * base default - by raising it to the feature minimum when it falls short (a monotone max, never a
 * lower). This is deliberately NOT gated on "was it a default?": a profile that pins RX_BUF_SIZE to
 * fit its DRAM must still be lifted to the streaming floor when streaming is built, or the pin
 * silently deadlocks large uploads. A value already at or above the floor is left untouched, so an
 * intentionally roomy ring is preserved.
 */

#ifndef PROTOCORE_DERIVED_SIZING_H
#define PROTOCORE_DERIVED_SIZING_H

// --- RX ring (RX_BUF_SIZE): hold the largest first-flight / window any enabled feature needs ---

// Streamed uploads need the RX ring to hold a full TCP receive window or the peer overruns it and
// the transfer deadlocks (ack-on-consume reopens the window only as the ring drains). The window is
// ~5.7 KB, so the ring must comfortably exceed it.
#if PC_ENABLE_STREAM_BODY && RX_BUF_SIZE < 8192
#undef RX_BUF_SIZE
#define RX_BUF_SIZE 8192
#endif

// A modern SSH client's first flight (identification banner + KEXINIT) is ~1.5 KB: post-quantum /
// curve kex names, cert host-key algs, EtM MACs, ext-info-c. The RX ring must hold it or the
// handshake resets at key exchange.
#if PC_ENABLE_SSH && RX_BUF_SIZE < 2048
#undef RX_BUF_SIZE
#define RX_BUF_SIZE 2048
#endif

// A modern TLS ClientHello (TLS 1.3 key shares + cipher/sig-alg lists + the RFC 7685 padding real
// clients send) is ~1.5 KB and arrives as one TCP segment. The recv callback refuses a whole segment
// that will not fit the ring (ERR_MEM, lossless backpressure), so a ClientHello bigger than the ring
// is refused forever and the handshake stalls to an idle-timeout RST - every 1.3-leading client
// (curl, browsers, Python) then fails to connect while a 1.2-only client squeaks in. The ring must
// hold a full segment.
#if PC_ENABLE_TLS && RX_BUF_SIZE < 2048
#undef RX_BUF_SIZE
#define RX_BUF_SIZE 2048
#endif

#endif // PROTOCORE_DERIVED_SIZING_H
