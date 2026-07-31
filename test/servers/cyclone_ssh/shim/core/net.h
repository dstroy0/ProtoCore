// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Test tooling only, NOT part of the library; builds against Oryx CycloneSSH/CycloneCRYPTO
// (GPLv2), fetched at run time by run_interop.sh - never vendored here.
#ifndef _NET_H
#define _NET_H

// POSIX socket shim standing in for CycloneTCP's core/net.h.
//
// CycloneSSH's SshContext holds a CycloneTCP "Socket *" and calls the CycloneTCP socket API
// directly (there is no socket-callback abstraction like CycloneSSL's tlsSetSocketCallbacks).
// Rather than pull in the whole CycloneTCP stack, this header + socket.c provide exactly the
// socket surface the CycloneSSH *client* path uses, backed by BSD sockets, returning Cyclone
// error_t codes. ssh.h includes "core/net.h", so this file supplies the types and prototypes
// it expects.

#include "error.h"
#include "os_port.h"

// --- socket type / protocol / shutdown selectors ---
#define SOCKET_TYPE_STREAM 1
#define SOCKET_TYPE_DGRAM 2

#define SOCKET_IP_PROTO_TCP 6
#define SOCKET_IP_PROTO_UDP 17

#define SOCKET_SD_RECEIVE 0
#define SOCKET_SD_SEND 1
#define SOCKET_SD_BOTH 2

// --- socketReceive flags. Only BREAK_CRLF is exercised by the client: it is used to read the
//     server's SSH identification line ("SSH-2.0-...\r\n") without swallowing the binary KEXINIT
//     that follows it. The low byte carries the break character. ---
#define SOCKET_FLAG_WAIT_ALL 0x00000800
#define SOCKET_FLAG_BREAK_CHAR 0x00001000
#define SOCKET_FLAG_BREAK(c) (SOCKET_FLAG_BREAK_CHAR | ((c) & 0xFF))
#define SOCKET_FLAG_BREAK_CRLF SOCKET_FLAG_BREAK('\n')

// --- socketPoll event bits: eventMask is requested, eventFlags is returned ---
#define SOCKET_EVENT_TIMEOUT 0x0000
#define SOCKET_EVENT_ACCEPT 0x0100
#define SOCKET_EVENT_RX_READY 0x0200
#define SOCKET_EVENT_TX_READY 0x0400
#define SOCKET_EVENT_CLOSED 0x0800

// --- IP address. Field names match the CycloneTCP layout the tree references. ---
typedef uint32_t Ipv4Addr;

typedef struct
{
    uint8_t b[16];
} Ipv6Addr;

typedef struct
{
    size_t length; // 4 => IPv4, 16 => IPv6
    union {
        Ipv4Addr ipv4Addr; // stored in network byte order
        Ipv6Addr ipv6Addr;
    };
} IpAddr;

// Opaque stack handles. The POSIX shim ignores them.
typedef struct NetInterface NetInterface;
typedef struct NetContext NetContext;

// The shim Socket: a struct wrapping a BSD file descriptor plus the current timeout.
typedef struct
{
    int fd;            // BSD socket descriptor (-1 once closed)
    int type;          // SOCKET_TYPE_*
    int protocol;      // SOCKET_IP_PROTO_*
    systime_t timeout; // current blocking timeout in ms (0 => non-blocking)
} Socket;

// Descriptor handed to socketPoll: which socket, which events wanted, which fired.
typedef struct
{
    Socket *socket;
    uint_t eventMask;
    uint_t eventFlags;
} SocketEventDesc;

// Server-only sentinel (referenced only under SSH_SERVER_SUPPORT, which is disabled).
extern const IpAddr IP_ADDR_ANY;

#ifdef __cplusplus
extern "C"
{
#endif

    NetContext *netGetDefaultContext(void);

    Socket *socketOpen(uint_t type, uint_t protocol);
    Socket *socketOpenEx(NetContext *netContext, uint_t type, uint_t protocol);
    error_t socketSetTimeout(Socket *socket, systime_t timeout);
    error_t socketBindToInterface(Socket *socket, NetInterface *interface);
    error_t socketConnect(Socket *socket, const IpAddr *remoteIpAddr, uint16_t remotePort);
    error_t socketSend(Socket *socket, const void *data, size_t length, size_t *written, uint_t flags);
    error_t socketReceive(Socket *socket, void *data, size_t size, size_t *received, uint_t flags);
    error_t socketPoll(SocketEventDesc *eventDesc, uint_t size, OsEvent *extEvent, systime_t timeout);
    error_t socketShutdown(Socket *socket, uint_t how);
    void socketClose(Socket *socket);
    error_t socketGetRemoteAddr(Socket *socket, IpAddr *ipAddr, uint16_t *port);
    error_t ipStringToAddr(const char_t *str, IpAddr *ipAddr);

#ifdef __cplusplus
}
#endif

#endif
