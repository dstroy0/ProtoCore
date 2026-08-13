// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file phase_machine.c
 * @brief The handshake phase machine, RFC 4253 sec 4.2 through sec 10.
 */

#include "network_drivers/presentation/ssh/transport/phase_machine.h"
#include "network_drivers/presentation/ssh/transport/transport.h"

SshPhase ssh_phase(uint8_t i)
{
    return (i < MAX_SSH_CONNS) ? ssh_sess[i].phase : SSH_PHASE_IDENT;
}

proto_bool ssh_phase_is(uint8_t i, SshPhase p)
{
    return i < MAX_SSH_CONNS && ssh_sess[i].phase == p;
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

void ssh_phase_reset(uint8_t i)
{
    if (i < MAX_SSH_CONNS)
    {
        // Nothing to resume yet: the first exchange ends at the sec 10 service request.
        ssh_sess[i].phase_before_kex = SSH_PHASE_SERVICE;
    }
    phase_set(i, SSH_PHASE_IDENT);
}

void ssh_phase_ident_done(uint8_t i)
{
    phase_set(i, SSH_PHASE_KEXINIT);
}

// Reached from an established connection when the peer opens the re-exchange, so this is the other
// place a phase worth resuming can still be current.
void ssh_phase_kexinit_done(uint8_t i)
{
    remember_phase(i);
    phase_set(i, SSH_PHASE_DH_INIT);
}

void ssh_phase_kex_done(uint8_t i)
{
    phase_set(i, SSH_PHASE_NEWKEYS);
}

// sec 9: "the state of the higher-level protocols is not affected by the key exchange." The
// connection resumes exactly where the exchange interrupted it - which for the first exchange is
// the sec 10 service request, and for a re-exchange is whatever was running, including an RFC 4252
// authentication still in flight.
void ssh_phase_newkeys_done(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    phase_set(i, ssh_sess[i].phase_before_kex);
}

void ssh_phase_service_done(uint8_t i)
{
    phase_set(i, SSH_PHASE_AUTH);
}

// Authentication succeeding is one event: the connection is authenticated and the channel protocol
// may start, so both are recorded here rather than at each call site.
void ssh_phase_auth_done(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    ssh_sess[i].authed = PROTO_TRUE;
    phase_set(i, SSH_PHASE_OPEN);
}

// This end opens the re-exchange, so the phase it interrupts is still current here.
void ssh_phase_rekey_begin(uint8_t i)
{
    remember_phase(i);
    phase_set(i, SSH_PHASE_KEXINIT);
}

// ---------------------------------------------------------------------------
// Admissibility
// ---------------------------------------------------------------------------

proto_bool ssh_phase_admits_ident(uint8_t i)
{
    return ssh_phase_is(i, SSH_PHASE_IDENT);
}

// An exchange is in flight from the KEXINIT that starts it to the NEWKEYS that ends it, and before
// the identification strings none can run at all.
//
// RFC 4253 sec 7.1: "a party MUST respond with its own SSH_MSG_KEXINIT message, except when the
// received SSH_MSG_KEXINIT already was a reply." Whether it is a reply is exactly whether this end
// has already sent its own, which kexinit_sent records; the phase alone cannot tell the two apart.
proto_bool ssh_phase_admits_kexinit(uint8_t i)
{
    const SshPhase p = ssh_phase(i);
    return p != SSH_PHASE_IDENT && p != SSH_PHASE_DH_INIT && p != SSH_PHASE_NEWKEYS;
}

proto_bool ssh_kexinit_needs_reply(uint8_t i)
{
    return i < MAX_SSH_CONNS && !ssh_sess[i].kexinit_sent;
}

proto_bool ssh_phase_admits_kexdh_init(uint8_t i)
{
    return ssh_phase_is(i, SSH_PHASE_DH_INIT);
}

proto_bool ssh_phase_admits_newkeys(uint8_t i)
{
    return ssh_phase_is(i, SSH_PHASE_NEWKEYS);
}

proto_bool ssh_phase_admits_service_request(uint8_t i)
{
    return ssh_phase_is(i, SSH_PHASE_SERVICE);
}

proto_bool ssh_phase_admits_userauth(uint8_t i)
{
    return ssh_phase_is(i, SSH_PHASE_AUTH);
}

// RFC 4252 sec 5.1 asks whether SUCCESS has been sent, which is what authed records. The phase does
// not answer it: a re-exchange walks an authenticated connection back through KEXINIT and NEWKEYS
// (RFC 4253 sec 9) while it stays authenticated the whole way.
proto_bool ssh_phase_auth_complete(uint8_t i)
{
    return i < MAX_SSH_CONNS && ssh_sess[i].authed;
}

proto_bool ssh_phase_admits_rekey(uint8_t i)
{
    return i < MAX_SSH_CONNS && ssh_sess[i].phase == SSH_PHASE_OPEN && !ssh_sess[i].kex_active;
}

proto_bool ssh_phase_is_open(uint8_t i)
{
    return ssh_phase_is(i, SSH_PHASE_OPEN);
}
