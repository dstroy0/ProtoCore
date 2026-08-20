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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SSH

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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

/** @brief What get takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineGetArgs;
/** @brief What is takes: i, p. */
typedef struct
{
    uint8_t i;
    SshPhase p;
} PhaseMachineIsArgs;
/** @brief What reset takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineResetArgs;
/** @brief What ident_done takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineIdentDoneArgs;
/** @brief What kexinit_done takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineKexinitDoneArgs;
/** @brief What kex_done takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineKexDoneArgs;
/** @brief What newkeys_done takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineNewkeysDoneArgs;
/** @brief What service_done takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineServiceDoneArgs;
/** @brief What auth_done takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineAuthDoneArgs;
/** @brief What rekey_begin takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineRekeyBeginArgs;
/** @brief What admits_ident takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineAdmitsIdentArgs;
/** @brief What admits_kexinit takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineAdmitsKexinitArgs;
/** @brief What kexinit_needs_reply takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineKexinitNeedsReplyArgs;
/** @brief What admits_kexdh_init takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineAdmitsKexdhInitArgs;
/** @brief What admits_newkeys takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineAdmitsNewkeysArgs;
/** @brief What admits_service_request takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineAdmitsServiceRequestArgs;
/** @brief What admits_userauth takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineAdmitsUserauthArgs;
/** @brief What auth_complete takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineAuthCompleteArgs;
/** @brief What admits_rekey takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineAdmitsRekeyArgs;
/** @brief What is_open takes: i. */
typedef struct
{
    uint8_t i;
} PhaseMachineIsOpenArgs;
/**
 * @brief The handshake phase machine, RFC 4253 sec 4.2 through sec 10.
 *
 * A caller sets the members a call takes, invokes it through ::PhaseMachine with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   PhaseMachine.get_args.i = ...;
 *   PhaseMachine.get(work);
 *   // PhaseMachine.value is what the call reports
 *
 * @var PhaseMachineNs::get_args  what get takes: i
 * @var PhaseMachineNs::is_args  what is takes: i, p
 * @var PhaseMachineNs::reset_args  what reset takes: i
 * @var PhaseMachineNs::ident_done_args  what ident_done takes: i
 * @var PhaseMachineNs::kexinit_done_args  what kexinit_done takes: i
 * @var PhaseMachineNs::kex_done_args  what kex_done takes: i
 * @var PhaseMachineNs::newkeys_done_args  what newkeys_done takes: i
 * @var PhaseMachineNs::service_done_args  what service_done takes: i
 * @var PhaseMachineNs::auth_done_args  what auth_done takes: i
 * @var PhaseMachineNs::rekey_begin_args  what rekey_begin takes: i
 * @var PhaseMachineNs::admits_ident_args  what admits_ident takes: i
 * @var PhaseMachineNs::admits_kexinit_args  what admits_kexinit takes: i
 * @var PhaseMachineNs::kexinit_needs_reply_args  what kexinit_needs_reply takes: i
 * @var PhaseMachineNs::admits_kexdh_init_args  what admits_kexdh_init takes: i
 * @var PhaseMachineNs::admits_newkeys_args  what admits_newkeys takes: i
 * @var PhaseMachineNs::admits_service_request_args  what admits_service_request takes: i
 * @var PhaseMachineNs::admits_userauth_args  what admits_userauth takes: i
 * @var PhaseMachineNs::auth_complete_args  what auth_complete takes: i
 * @var PhaseMachineNs::admits_rekey_args  what admits_rekey takes: i
 * @var PhaseMachineNs::is_open_args  what is_open takes: i
 * @var PhaseMachineNs::ok  a call's true/false outcome
 * @var PhaseMachineNs::value  the value a call reports
 * @var PhaseMachineNs::get  the phase slot i is in
 * @var PhaseMachineNs::is  true when slot i is in phase p
 * @var PhaseMachineNs::reset  start a connection at the identification exchange (RFC 4253 sec 4.2)
 * @var PhaseMachineNs::ident_done  the peer identification string is whole: sec 4.2 is done, sec 7.1 ...
 * @var PhaseMachineNs::kexinit_done  algorithms are negotiated (sec 7.1): await the exchange of sec 8
 * @var PhaseMachineNs::kex_done  the exchange produced K and H (sec 8): await NEWKEYS
 * @var PhaseMachineNs::newkeys_done  NEWKEYS ended the exchange (sec 7.3): the keys are in use. A first ...
 * @var PhaseMachineNs::service_done  the service request named "ssh-userauth" and was accepted (sec 10)
 * @var PhaseMachineNs::auth_done  authentication completed (RFC 4252 sec 5.1 SUCCESS): the channel ...
 * @var PhaseMachineNs::rekey_begin  A key re-exchange begins on an established connection (sec 9)
 * @var PhaseMachineNs::admits_ident  RFC 4253 sec 4.2: identification bytes are only read before the ...
 * @var PhaseMachineNs::admits_kexinit  RFC 4253 sec 7.1: a KEXINIT starts an exchange, and one is refused ...
 * @var PhaseMachineNs::kexinit_needs_reply  RFC 4253 sec 7.1: does an inbound KEXINIT still need our own in ...
 * @var PhaseMachineNs::admits_kexdh_init  RFC 4253 sec 8: KEXDH_INIT belongs to an exchange that has been ...
 * @var PhaseMachineNs::admits_newkeys  RFC 4253 sec 7.3: "NEWKEYS ends a key exchange", so one outside an ...
 * @var PhaseMachineNs::admits_service_request  RFC 4253 sec 10: a service request is only valid once the exchange ...
 * @var PhaseMachineNs::admits_userauth  RFC 4252: a userauth request belongs to the authentication phase
 * @var PhaseMachineNs::auth_complete  RFC 4252 sec 5.1: a request arriving after SUCCESS is ignored ...
 * @var PhaseMachineNs::admits_rekey  RFC 4253 sec 9: a re-exchange starts only on an established ...
 * @var PhaseMachineNs::is_open  RFC 4254: the channel protocol runs once the connection is open
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    PhaseMachineGetArgs get_args;
    PhaseMachineIsArgs is_args;
    PhaseMachineResetArgs reset_args;
    PhaseMachineIdentDoneArgs ident_done_args;
    PhaseMachineKexinitDoneArgs kexinit_done_args;
    PhaseMachineKexDoneArgs kex_done_args;
    PhaseMachineNewkeysDoneArgs newkeys_done_args;
    PhaseMachineServiceDoneArgs service_done_args;
    PhaseMachineAuthDoneArgs auth_done_args;
    PhaseMachineRekeyBeginArgs rekey_begin_args;
    PhaseMachineAdmitsIdentArgs admits_ident_args;
    PhaseMachineAdmitsKexinitArgs admits_kexinit_args;
    PhaseMachineKexinitNeedsReplyArgs kexinit_needs_reply_args;
    PhaseMachineAdmitsKexdhInitArgs admits_kexdh_init_args;
    PhaseMachineAdmitsNewkeysArgs admits_newkeys_args;
    PhaseMachineAdmitsServiceRequestArgs admits_service_request_args;
    PhaseMachineAdmitsUserauthArgs admits_userauth_args;
    PhaseMachineAuthCompleteArgs auth_complete_args;
    PhaseMachineAdmitsRekeyArgs admits_rekey_args;
    PhaseMachineIsOpenArgs is_open_args;
    proto_bool ok;
    SshPhase value;
} PhaseMachineVars;

/** @brief The operands and the outcome. */
extern PhaseMachineVars PhaseMachineV;

/** @brief The entries. */
typedef struct
{
    void (*const get)(uint8_t *restrict work);
    void (*const is)(uint8_t *restrict work);
    void (*const reset)(uint8_t *restrict work);
    void (*const ident_done)(uint8_t *restrict work);
    void (*const kexinit_done)(uint8_t *restrict work);
    void (*const kex_done)(uint8_t *restrict work);
    void (*const newkeys_done)(uint8_t *restrict work);
    void (*const service_done)(uint8_t *restrict work);
    void (*const auth_done)(uint8_t *restrict work);
    void (*const rekey_begin)(uint8_t *restrict work);
    void (*const admits_ident)(uint8_t *restrict work);
    void (*const admits_kexinit)(uint8_t *restrict work);
    void (*const kexinit_needs_reply)(uint8_t *restrict work);
    void (*const admits_kexdh_init)(uint8_t *restrict work);
    void (*const admits_newkeys)(uint8_t *restrict work);
    void (*const admits_service_request)(uint8_t *restrict work);
    void (*const admits_userauth)(uint8_t *restrict work);
    void (*const auth_complete)(uint8_t *restrict work);
    void (*const admits_rekey)(uint8_t *restrict work);
    void (*const is_open)(uint8_t *restrict work);
} PhaseMachineNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in PhaseMachineV or a region of the borrow at a fixed offset.
void protocore_phase_machine_get(uint8_t *restrict work);
void protocore_phase_machine_is(uint8_t *restrict work);
void protocore_phase_machine_reset(uint8_t *restrict work);
void protocore_phase_machine_ident_done(uint8_t *restrict work);
void protocore_phase_machine_kexinit_done(uint8_t *restrict work);
void protocore_phase_machine_kex_done(uint8_t *restrict work);
void protocore_phase_machine_newkeys_done(uint8_t *restrict work);
void protocore_phase_machine_service_done(uint8_t *restrict work);
void protocore_phase_machine_auth_done(uint8_t *restrict work);
void protocore_phase_machine_rekey_begin(uint8_t *restrict work);
void protocore_phase_machine_admits_ident(uint8_t *restrict work);
void protocore_phase_machine_admits_kexinit(uint8_t *restrict work);
void protocore_phase_machine_kexinit_needs_reply(uint8_t *restrict work);
void protocore_phase_machine_admits_kexdh_init(uint8_t *restrict work);
void protocore_phase_machine_admits_newkeys(uint8_t *restrict work);
void protocore_phase_machine_admits_service_request(uint8_t *restrict work);
void protocore_phase_machine_admits_userauth(uint8_t *restrict work);
void protocore_phase_machine_auth_complete(uint8_t *restrict work);
void protocore_phase_machine_admits_rekey(uint8_t *restrict work);
void protocore_phase_machine_is_open(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `PhaseMachine.get(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const PhaseMachineNs PhaseMachine __attribute__((unused)) = {
    .get = protocore_phase_machine_get,
    .is = protocore_phase_machine_is,
    .reset = protocore_phase_machine_reset,
    .ident_done = protocore_phase_machine_ident_done,
    .kexinit_done = protocore_phase_machine_kexinit_done,
    .kex_done = protocore_phase_machine_kex_done,
    .newkeys_done = protocore_phase_machine_newkeys_done,
    .service_done = protocore_phase_machine_service_done,
    .auth_done = protocore_phase_machine_auth_done,
    .rekey_begin = protocore_phase_machine_rekey_begin,
    .admits_ident = protocore_phase_machine_admits_ident,
    .admits_kexinit = protocore_phase_machine_admits_kexinit,
    .kexinit_needs_reply = protocore_phase_machine_kexinit_needs_reply,
    .admits_kexdh_init = protocore_phase_machine_admits_kexdh_init,
    .admits_newkeys = protocore_phase_machine_admits_newkeys,
    .admits_service_request = protocore_phase_machine_admits_service_request,
    .admits_userauth = protocore_phase_machine_admits_userauth,
    .auth_complete = protocore_phase_machine_auth_complete,
    .admits_rekey = protocore_phase_machine_admits_rekey,
    .is_open = protocore_phase_machine_is_open,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH

#endif // PROTOCORE_TRANSPORT_PHASE_MACHINE_H
