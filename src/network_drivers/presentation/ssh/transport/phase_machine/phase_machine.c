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

void protocore_phase_machine_get(uint8_t *restrict work);
void protocore_phase_machine_is(uint8_t *restrict work);

void protocore_phase_machine_get(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.get_args.i;

    PhaseMachineV.value = (i < MAX_SSH_CONNS) ? ssh_sess[i].phase : SSH_PHASE_IDENT;
}

void protocore_phase_machine_is(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.is_args.i;
    SshPhase p = PhaseMachineV.is_args.p;

    PhaseMachineV.ok = i < MAX_SSH_CONNS && ssh_sess[i].phase == p;
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

void protocore_phase_machine_reset(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.reset_args.i;

    if (i < MAX_SSH_CONNS)
    {
        // Nothing to resume yet: the first exchange ends at the sec 10 service request.
        ssh_sess[i].phase_before_kex = SSH_PHASE_SERVICE;
    }
    phase_set(i, SSH_PHASE_IDENT);
}

void protocore_phase_machine_ident_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.ident_done_args.i;

    phase_set(i, SSH_PHASE_KEXINIT);
}

// Reached from an established connection when the peer opens the re-exchange, so this is the other
// place a phase worth resuming can still be current.
void protocore_phase_machine_kexinit_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.kexinit_done_args.i;

    remember_phase(i);
    phase_set(i, SSH_PHASE_DH_INIT);
}

void protocore_phase_machine_kex_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.kex_done_args.i;

    phase_set(i, SSH_PHASE_NEWKEYS);
}

// sec 9: "the state of the higher-level protocols is not affected by the key exchange." The
// connection resumes exactly where the exchange interrupted it - which for the first exchange is
// the sec 10 service request, and for a re-exchange is whatever was running, including an RFC 4252
// authentication still in flight.
void protocore_phase_machine_newkeys_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.newkeys_done_args.i;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    phase_set(i, ssh_sess[i].phase_before_kex);
}

void protocore_phase_machine_service_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.service_done_args.i;

    phase_set(i, SSH_PHASE_AUTH);
}

// Authentication succeeding is one event: the connection is authenticated and the channel protocol
// may start, so both are recorded here rather than at each call site.
void protocore_phase_machine_auth_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.auth_done_args.i;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    ssh_sess[i].authed = PROTO_TRUE;
    phase_set(i, SSH_PHASE_OPEN);
}

// This end opens the re-exchange, so the phase it interrupts is still current here.
void protocore_phase_machine_rekey_begin(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.rekey_begin_args.i;

    remember_phase(i);
    phase_set(i, SSH_PHASE_KEXINIT);
}

// ---------------------------------------------------------------------------
// Admissibility
// ---------------------------------------------------------------------------

void protocore_phase_machine_admits_ident(uint8_t *restrict work)
{
    uint8_t i = PhaseMachineV.admits_ident_args.i;

    PhaseMachineV.is_args.i = i;
    PhaseMachineV.is_args.p = SSH_PHASE_IDENT;
    protocore_phase_machine_is(work);
}

// An exchange is in flight from the KEXINIT that starts it to the NEWKEYS that ends it, and before
// the identification strings none can run at all.
//
// RFC 4253 sec 7.1: "a party MUST respond with its own SSH_MSG_KEXINIT message, except when the
// received SSH_MSG_KEXINIT already was a reply." Whether it is a reply is exactly whether this end
// has already sent its own, which kexinit_sent records; the phase alone cannot tell the two apart.
void protocore_phase_machine_admits_kexinit(uint8_t *restrict work)
{
    uint8_t i = PhaseMachineV.admits_kexinit_args.i;

    PhaseMachineV.get_args.i = i;
    protocore_phase_machine_get(work);
    const SshPhase p = PhaseMachineV.value;
    PhaseMachineV.ok = p != SSH_PHASE_IDENT && p != SSH_PHASE_DH_INIT && p != SSH_PHASE_NEWKEYS;
}

void protocore_phase_machine_kexinit_needs_reply(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.kexinit_needs_reply_args.i;

    PhaseMachineV.ok = i < MAX_SSH_CONNS && !ssh_sess[i].kexinit_sent;
}

void protocore_phase_machine_admits_kexdh_init(uint8_t *restrict work)
{
    uint8_t i = PhaseMachineV.admits_kexdh_init_args.i;

    PhaseMachineV.is_args.i = i;
    PhaseMachineV.is_args.p = SSH_PHASE_DH_INIT;
    protocore_phase_machine_is(work);
}

void protocore_phase_machine_admits_newkeys(uint8_t *restrict work)
{
    uint8_t i = PhaseMachineV.admits_newkeys_args.i;

    PhaseMachineV.is_args.i = i;
    PhaseMachineV.is_args.p = SSH_PHASE_NEWKEYS;
    protocore_phase_machine_is(work);
}

void protocore_phase_machine_admits_service_request(uint8_t *restrict work)
{
    uint8_t i = PhaseMachineV.admits_service_request_args.i;

    PhaseMachineV.is_args.i = i;
    PhaseMachineV.is_args.p = SSH_PHASE_SERVICE;
    protocore_phase_machine_is(work);
}

void protocore_phase_machine_admits_userauth(uint8_t *restrict work)
{
    uint8_t i = PhaseMachineV.admits_userauth_args.i;

    PhaseMachineV.is_args.i = i;
    PhaseMachineV.is_args.p = SSH_PHASE_AUTH;
    protocore_phase_machine_is(work);
}

// RFC 4252 sec 5.1 asks whether SUCCESS has been sent, which is what authed records. The phase does
// not answer it: a re-exchange walks an authenticated connection back through KEXINIT and NEWKEYS
// (RFC 4253 sec 9) while it stays authenticated the whole way.
void protocore_phase_machine_auth_complete(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.auth_complete_args.i;

    PhaseMachineV.ok = i < MAX_SSH_CONNS && ssh_sess[i].authed;
}

void protocore_phase_machine_admits_rekey(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = PhaseMachineV.admits_rekey_args.i;

    PhaseMachineV.ok = i < MAX_SSH_CONNS && ssh_sess[i].phase == SSH_PHASE_OPEN && !ssh_sess[i].kex_active;
}

void protocore_phase_machine_is_open(uint8_t *restrict work)
{
    uint8_t i = PhaseMachineV.is_open_args.i;

    PhaseMachineV.is_args.i = i;
    PhaseMachineV.is_args.p = SSH_PHASE_OPEN;
    protocore_phase_machine_is(work);
}
/** @brief The operands and the outcome. */
PhaseMachineVars PhaseMachineV;

PROTOCORE_END_DECLS
