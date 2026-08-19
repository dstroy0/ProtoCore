// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file phase_machine.c
 * @brief The handshake phase machine, RFC 4253 sec 4.2 through sec 10.
 */

#include "network_drivers/presentation/ssh/transport/phase_machine/phase_machine.h"
#include "network_drivers/presentation/ssh/common.h"
#include "network_drivers/presentation/ssh/transport/transport/transport.h"

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void phase_machine_get(uint8_t *restrict work);
static void phase_machine_is(uint8_t *restrict work);

static void phase_machine_get(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.get_args.i;

    PhaseMachine.value = (i < MAX_SSH_CONNS) ? ssh_sess[i].phase : SSH_PHASE_IDENT;
}

static void phase_machine_is(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.is_args.i;
    SshPhase p = PhaseMachine.is_args.p;

    PhaseMachine.ok = i < MAX_SSH_CONNS && ssh_sess[i].phase == p;
}

// The one writer. Every advance below goes through it, so a phase change is a single site to read.
static void phase_set(uint8_t i, SshPhase p)
{
    if (i < MAX_SSH_CONNS)
    {
        ssh_sess[i].phase = p;
    }
}

// ---------------------------------------------------------------------------
// Advances
// ---------------------------------------------------------------------------

// A re-exchange interrupts an established connection and must hand it back unchanged (sec 9). Only
// the phases that outlive an exchange are worth remembering; during the first one the phase is part
// of the handshake itself, and the connection resumes at the sec 10 service request instead.
static void remember_phase(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    const SshPhase p = ssh_sess[i].phase;
    if (p == SSH_PHASE_SERVICE || p == SSH_PHASE_AUTH || p == SSH_PHASE_OPEN)
    {
        ssh_sess[i].phase_before_kex = p;
    }
}

static void phase_machine_reset(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.reset_args.i;

    if (i < MAX_SSH_CONNS)
    {
        // Nothing to resume yet: the first exchange ends at the sec 10 service request.
        ssh_sess[i].phase_before_kex = SSH_PHASE_SERVICE;
    }
    phase_set(i, SSH_PHASE_IDENT);
}

static void phase_machine_ident_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.ident_done_args.i;

    phase_set(i, SSH_PHASE_KEXINIT);
}

// Reached from an established connection when the peer opens the re-exchange, so this is the other
// place a phase worth resuming can still be current.
static void phase_machine_kexinit_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.kexinit_done_args.i;

    remember_phase(i);
    phase_set(i, SSH_PHASE_DH_INIT);
}

static void phase_machine_kex_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.kex_done_args.i;

    phase_set(i, SSH_PHASE_NEWKEYS);
}

// sec 9: "the state of the higher-level protocols is not affected by the key exchange." The
// connection resumes exactly where the exchange interrupted it - which for the first exchange is
// the sec 10 service request, and for a re-exchange is whatever was running, including an RFC 4252
// authentication still in flight.
static void phase_machine_newkeys_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.newkeys_done_args.i;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    phase_set(i, ssh_sess[i].phase_before_kex);
}

static void phase_machine_service_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.service_done_args.i;

    phase_set(i, SSH_PHASE_AUTH);
}

// Authentication succeeding is one event: the connection is authenticated and the channel protocol
// may start, so both are recorded here rather than at each call site.
static void phase_machine_auth_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.auth_done_args.i;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    ssh_sess[i].authed = PROTO_TRUE;
    phase_set(i, SSH_PHASE_OPEN);
}

// This end opens the re-exchange, so the phase it interrupts is still current here.
static void phase_machine_rekey_begin(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.rekey_begin_args.i;

    remember_phase(i);
    phase_set(i, SSH_PHASE_KEXINIT);
}

// ---------------------------------------------------------------------------
// Admissibility
// ---------------------------------------------------------------------------

static void phase_machine_admits_ident(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t i = PhaseMachine.admits_ident_args.i;

    PhaseMachine.is_args.i = i;
    PhaseMachine.is_args.p = SSH_PHASE_IDENT;
    phase_machine_is(work);
}

// An exchange is in flight from the KEXINIT that starts it to the NEWKEYS that ends it, and before
// the identification strings none can run at all.
//
// RFC 4253 sec 7.1: "a party MUST respond with its own SSH_MSG_KEXINIT message, except when the
// received SSH_MSG_KEXINIT already was a reply." Whether it is a reply is exactly whether this end
// has already sent its own, which kexinit_sent records; the phase alone cannot tell the two apart.
static void phase_machine_admits_kexinit(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t i = PhaseMachine.admits_kexinit_args.i;

    PhaseMachine.get_args.i = i;
    phase_machine_get(work);
    const SshPhase p = PhaseMachine.value;
    PhaseMachine.ok = p != SSH_PHASE_IDENT && p != SSH_PHASE_DH_INIT && p != SSH_PHASE_NEWKEYS;
}

static void phase_machine_kexinit_needs_reply(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.kexinit_needs_reply_args.i;

    PhaseMachine.ok = i < MAX_SSH_CONNS && !ssh_sess[i].kexinit_sent;
}

static void phase_machine_admits_kexdh_init(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t i = PhaseMachine.admits_kexdh_init_args.i;

    PhaseMachine.is_args.i = i;
    PhaseMachine.is_args.p = SSH_PHASE_DH_INIT;
    phase_machine_is(work);
}

static void phase_machine_admits_newkeys(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t i = PhaseMachine.admits_newkeys_args.i;

    PhaseMachine.is_args.i = i;
    PhaseMachine.is_args.p = SSH_PHASE_NEWKEYS;
    phase_machine_is(work);
}

static void phase_machine_admits_service_request(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t i = PhaseMachine.admits_service_request_args.i;

    PhaseMachine.is_args.i = i;
    PhaseMachine.is_args.p = SSH_PHASE_SERVICE;
    phase_machine_is(work);
}

static void phase_machine_admits_userauth(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t i = PhaseMachine.admits_userauth_args.i;

    PhaseMachine.is_args.i = i;
    PhaseMachine.is_args.p = SSH_PHASE_AUTH;
    phase_machine_is(work);
}

// RFC 4252 sec 5.1 asks whether SUCCESS has been sent, which is what authed records. The phase does
// not answer it: a re-exchange walks an authenticated connection back through KEXINIT and NEWKEYS
// (RFC 4253 sec 9) while it stays authenticated the whole way.
static void phase_machine_auth_complete(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.auth_complete_args.i;

    PhaseMachine.ok = i < MAX_SSH_CONNS && ssh_sess[i].authed;
}

static void phase_machine_admits_rekey(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachine.admits_rekey_args.i;

    PhaseMachine.ok = i < MAX_SSH_CONNS && ssh_sess[i].phase == SSH_PHASE_OPEN && !ssh_sess[i].kex_active;
}

static void phase_machine_is_open(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t i = PhaseMachine.is_open_args.i;

    PhaseMachine.is_args.i = i;
    PhaseMachine.is_args.p = SSH_PHASE_OPEN;
    phase_machine_is(work);
}
PhaseMachineNs PhaseMachine = {
    .get = phase_machine_get,
    .is = phase_machine_is,
    .reset = phase_machine_reset,
    .ident_done = phase_machine_ident_done,
    .kexinit_done = phase_machine_kexinit_done,
    .kex_done = phase_machine_kex_done,
    .newkeys_done = phase_machine_newkeys_done,
    .service_done = phase_machine_service_done,
    .auth_done = phase_machine_auth_done,
    .rekey_begin = phase_machine_rekey_begin,
    .admits_ident = phase_machine_admits_ident,
    .admits_kexinit = phase_machine_admits_kexinit,
    .kexinit_needs_reply = phase_machine_kexinit_needs_reply,
    .admits_kexdh_init = phase_machine_admits_kexdh_init,
    .admits_newkeys = phase_machine_admits_newkeys,
    .admits_service_request = phase_machine_admits_service_request,
    .admits_userauth = phase_machine_admits_userauth,
    .auth_complete = phase_machine_auth_complete,
    .admits_rekey = phase_machine_admits_rekey,
    .is_open = phase_machine_is_open,
};

PROTOCORE_END_DECLS
