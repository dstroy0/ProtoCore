// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file phase_machine.h
 * @brief The handshake phase machine, RFC 4253 sec 4.2 through sec 10.
 *
 * One connection walks the transport layer's own progression, then hands over to RFC 4252:
 *
 *     IDENT (sec 4.2) -> KEXINIT (sec 7.1) -> DH_INIT (sec 8) -> NEWKEYS (sec 7.3)
 *       -> SERVICE (sec 10) -> AUTH (RFC 4252) -> OPEN (RFC 4254)
 *
 * and sec 9 sends an established connection back to KEXINIT for a re-exchange. The advance
 * functions are named for the RFC event that causes them and are the only writers of the phase;
 * the admits functions are the questions the dispatch asks before acting on a message.
 */

#ifndef PROTOCORE_TRANSPORT_PHASE_MACHINE_H
#define PROTOCORE_TRANSPORT_PHASE_MACHINE_H

#include "network_drivers/presentation/ssh/common.h"

PROTOCORE_BEGIN_DECLS

/** @brief SSH connection lifecycle phase. */
typedef enum PROTO_ENUM_PACKED
{
    SSH_PHASE_IDENT,   ///< Awaiting the peer identification string.
    SSH_PHASE_KEXINIT, ///< Awaiting the peer KEXINIT.
    SSH_PHASE_DH_INIT, ///< Awaiting SSH_MSG_KEXDH_INIT.
    SSH_PHASE_NEWKEYS, ///< Awaiting SSH_MSG_NEWKEYS.
    SSH_PHASE_SERVICE, ///< Awaiting SERVICE_REQUEST ("ssh-userauth").
    SSH_PHASE_AUTH,    ///< User authentication in progress (RFC 4252).
    SSH_PHASE_OPEN     ///< Authenticated; connection/channel protocol active.
} SshPhase;

/** @brief The phase slot @p i is in. */
SshPhase ssh_phase(uint8_t i);

/** @brief True when slot @p i is in phase @p p. */
proto_bool ssh_phase_is(uint8_t i, SshPhase p);

// ---------------------------------------------------------------------------
// Advances, each named for the RFC event that causes it
// ---------------------------------------------------------------------------

/** @brief Start a connection at the identification exchange (RFC 4253 sec 4.2). */
void ssh_phase_reset(uint8_t i);

/** @brief The peer identification string is whole: sec 4.2 is done, sec 7.1 begins. */
void ssh_phase_ident_done(uint8_t i);

/** @brief Algorithms are negotiated (sec 7.1): await the exchange of sec 8. */
void ssh_phase_kexinit_done(uint8_t i);

/** @brief The exchange produced K and H (sec 8): await NEWKEYS. */
void ssh_phase_kex_done(uint8_t i);

/**
 * @brief NEWKEYS ended the exchange (sec 7.3): the keys are in use.
 *
 * A first exchange leaves the connection awaiting the sec 10 service request; a re-exchange on an
 * already authenticated connection resumes the channel protocol it interrupted.
 */
void ssh_phase_newkeys_done(uint8_t i);

/** @brief The service request named "ssh-userauth" and was accepted (sec 10). */
void ssh_phase_service_done(uint8_t i);

/** @brief Authentication completed (RFC 4252 sec 5.1 SUCCESS): the channel protocol may start. */
void ssh_phase_auth_done(uint8_t i);

/** @brief A key re-exchange begins on an established connection (sec 9). */
void ssh_phase_rekey_begin(uint8_t i);

// ---------------------------------------------------------------------------
// Admissibility, the questions the dispatch asks
// ---------------------------------------------------------------------------

/** @brief RFC 4253 sec 4.2: identification bytes are only read before the binary packet protocol. */
proto_bool ssh_phase_admits_ident(uint8_t i);

/**
 * @brief RFC 4253 sec 7.1: a KEXINIT starts an exchange, and one is refused while another is
 * already in flight so the state in flight is not discarded.
 */
proto_bool ssh_phase_admits_kexinit(uint8_t i);

/**
 * @brief RFC 4253 sec 7.1: does an inbound KEXINIT still need our own in reply?
 *
 * "a party MUST respond with its own SSH_MSG_KEXINIT message, except when the received
 * SSH_MSG_KEXINIT already was a reply" - false once this end has sent its own, which is also when
 * sec 7.1 starts forbidding a further one.
 */
proto_bool ssh_kexinit_needs_reply(uint8_t i);

/** @brief RFC 4253 sec 8: KEXDH_INIT belongs to an exchange that has been negotiated. */
proto_bool ssh_phase_admits_kexdh_init(uint8_t i);

/** @brief RFC 4253 sec 7.3: "NEWKEYS ends a key exchange", so one outside an exchange ends nothing. */
proto_bool ssh_phase_admits_newkeys(uint8_t i);

/**
 * @brief RFC 4253 sec 10: a service request is only valid once the exchange has completed and
 * encryption is on, which stops a jump from the exchange straight to userauth in cleartext.
 */
proto_bool ssh_phase_admits_service_request(uint8_t i);

/** @brief RFC 4252: a userauth request belongs to the authentication phase. */
proto_bool ssh_phase_admits_userauth(uint8_t i);

/** @brief RFC 4252 sec 5.1: a request arriving after SUCCESS is ignored rather than answered. */
proto_bool ssh_phase_auth_complete(uint8_t i);

/** @brief RFC 4253 sec 9: a re-exchange starts only on an established connection with none running. */
proto_bool ssh_phase_admits_rekey(uint8_t i);

/** @brief RFC 4254: the channel protocol runs once the connection is open. */
proto_bool ssh_phase_is_open(uint8_t i);

PROTOCORE_END_DECLS

#endif // PROTOCORE_TRANSPORT_PHASE_MACHINE_H
