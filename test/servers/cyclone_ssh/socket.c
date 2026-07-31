// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Test tooling only, NOT part of the library; builds against Oryx CycloneSSH/CycloneCRYPTO
// (GPLv2), fetched at run time by run_interop.sh - never vendored here.
// POSIX BSD-socket implementation of the CycloneTCP socket surface CycloneSSH's client uses.
//
// Every function maps a CycloneTCP socket call to a BSD-socket call and translates the result
// into a Cyclone error_t. A "Socket" is just a heap-allocated struct wrapping an int fd plus the
// currently configured timeout (0 => non-blocking). The CycloneSSH client runs the socket
// non-blocking during the session and waits for readiness with socketPoll (a select() wrapper).

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "core/net.h"

// CycloneTCP's sentinel for "block forever".
#define SHIM_INFINITE_DELAY 0xFFFFFFFFU

// Server-side sentinel; never dereferenced with SSH_SERVER_SUPPORT disabled.
const IpAddr IP_ADDR_ANY = {4, {0}};

// The CycloneSSH client passes netGetDefaultContext() to socketOpenEx; the POSIX shim needs no
// per-stack context, so this is a harmless NULL.
NetContext *netGetDefaultContext(void)
{
   return NULL;
}

// Apply the socket's timeout to the fd: 0 => O_NONBLOCK, otherwise blocking with SO_RCVTIMEO /
// SO_SNDTIMEO. This is the crux of matching CycloneTCP semantics: the client sets the timeout to
// context->timeout while connecting, then to 0 (non-blocking) for the event-driven session.
static void apply_timeout(Socket *sock)
{
   int flags = fcntl(sock->fd, F_GETFL, 0);
   if(flags < 0)
      flags = 0;

   if(sock->timeout == 0)
   {
      fcntl(sock->fd, F_SETFL, flags | O_NONBLOCK);
   }
   else
   {
      struct timeval tv;

      fcntl(sock->fd, F_SETFL, flags & ~O_NONBLOCK);

      tv.tv_sec = sock->timeout / 1000;
      tv.tv_usec = (sock->timeout % 1000) * 1000;
      setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
   }
}

Socket *socketOpen(uint_t type, uint_t protocol)
{
   Socket *sock;
   int fd;
   int st = (type == SOCKET_TYPE_STREAM) ? SOCK_STREAM : SOCK_DGRAM;
   int pr = (protocol == SOCKET_IP_PROTO_UDP) ? IPPROTO_UDP : IPPROTO_TCP;

   fd = socket(AF_INET, st, pr);
   if(fd < 0)
      return NULL;

   sock = osAllocMem(sizeof(Socket));
   if(sock == NULL)
   {
      close(fd);
      return NULL;
   }

   sock->fd = fd;
   sock->type = (int) type;
   sock->protocol = (int) protocol;
   sock->timeout = 0;
   return sock;
}

Socket *socketOpenEx(NetContext *netContext, uint_t type, uint_t protocol)
{
   (void) netContext;
   return socketOpen(type, protocol);
}

error_t socketSetTimeout(Socket *socket, systime_t timeout)
{
   if(socket == NULL || socket->fd < 0)
      return ERROR_INVALID_PARAMETER;

   socket->timeout = timeout;
   apply_timeout(socket);
   return NO_ERROR;
}

error_t socketBindToInterface(Socket *socket, NetInterface *interface)
{
   (void) interface;
   if(socket == NULL)
      return ERROR_INVALID_PARAMETER;
   // No interface binding needed on a single-homed host.
   return NO_ERROR;
}

error_t socketConnect(Socket *socket, const IpAddr *remoteIpAddr, uint16_t remotePort)
{
   struct sockaddr_in sa;
   int r;

   if(socket == NULL || socket->fd < 0 || remoteIpAddr == NULL)
      return ERROR_INVALID_PARAMETER;

   memset(&sa, 0, sizeof(sa));
   sa.sin_family = AF_INET;
   sa.sin_port = htons(remotePort);
   sa.sin_addr.s_addr = remoteIpAddr->ipv4Addr; // already network byte order

   r = connect(socket->fd, (struct sockaddr *) &sa, sizeof(sa));
   if(r == 0)
   {
      // SSH is latency-sensitive: coalescing is handled at the packet layer.
      int one = 1;
      setsockopt(socket->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
      return NO_ERROR;
   }

   if(errno == EISCONN)
      return NO_ERROR;
   if(errno == EINPROGRESS || errno == EALREADY || errno == EAGAIN || errno == EWOULDBLOCK)
      return (socket->timeout == 0) ? ERROR_WOULD_BLOCK : ERROR_TIMEOUT;

   return ERROR_CONNECTION_FAILED;
}

error_t socketSend(Socket *socket, const void *data, size_t length, size_t *written,
   uint_t flags)
{
   ssize_t n;

   (void) flags;
   if(socket == NULL || socket->fd < 0)
      return ERROR_INVALID_PARAMETER;

   // MSG_NOSIGNAL: a broken pipe must surface as an error code, not a SIGPIPE.
   n = send(socket->fd, data, length, MSG_NOSIGNAL);
   if(n >= 0)
   {
      if(written != NULL)
         *written = (size_t) n;
      return NO_ERROR;
   }

   if(written != NULL)
      *written = 0;

   if(errno == EAGAIN || errno == EWOULDBLOCK)
      return (socket->timeout == 0) ? ERROR_WOULD_BLOCK : ERROR_TIMEOUT;
   if(errno == EPIPE || errno == ECONNRESET)
      return ERROR_CONNECTION_RESET;

   return ERROR_WRITE_FAILED;
}

error_t socketReceive(Socket *socket, void *data, size_t size, size_t *received,
   uint_t flags)
{
   if(socket == NULL || socket->fd < 0)
      return ERROR_INVALID_PARAMETER;

   *received = 0;
   if(size == 0)
      return NO_ERROR;

   // SSH identification line: read up to and including the break character ('\n'), byte by byte,
   // so the binary key-exchange bytes that immediately follow are left in the kernel buffer.
   if((flags & SOCKET_FLAG_BREAK_CHAR) != 0)
   {
      char brk = (char) (flags & 0xFF);
      uint8_t *out = (uint8_t *) data;
      size_t i;

      for(i = 0; i < size; i++)
      {
         ssize_t n = recv(socket->fd, &out[i], 1, 0);

         if(n == 1)
         {
            (*received)++;
            if(out[i] == brk)
               return NO_ERROR;
         }
         else if(n == 0)
         {
            return (*received > 0) ? NO_ERROR : ERROR_END_OF_STREAM;
         }
         else
         {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
               if(*received > 0)
                  return NO_ERROR;
               return (socket->timeout == 0) ? ERROR_WOULD_BLOCK : ERROR_TIMEOUT;
            }
            if(errno == ECONNRESET)
               return ERROR_CONNECTION_RESET;
            return ERROR_READ_FAILED;
         }
      }

      // Buffer filled before the break character; report what we have.
      return NO_ERROR;
   }

   {
      ssize_t n = recv(socket->fd, data, size, 0);

      if(n > 0)
      {
         *received = (size_t) n;
         return NO_ERROR;
      }
      if(n == 0)
      {
         // Orderly shutdown by the peer.
         return ERROR_END_OF_STREAM;
      }
      if(errno == EAGAIN || errno == EWOULDBLOCK)
         return (socket->timeout == 0) ? ERROR_WOULD_BLOCK : ERROR_TIMEOUT;
      if(errno == ECONNRESET)
         return ERROR_CONNECTION_RESET;
      return ERROR_READ_FAILED;
   }
}

error_t socketPoll(SocketEventDesc *eventDesc, uint_t size, OsEvent *extEvent,
   systime_t timeout)
{
   fd_set readfds;
   fd_set writefds;
   fd_set exceptfds;
   struct timeval tv;
   struct timeval *ptv;
   int maxfd = -1;
   int r;
   uint_t i;

   // The external cancellation event is unused by this single-threaded client.
   (void) extEvent;

   FD_ZERO(&readfds);
   FD_ZERO(&writefds);
   FD_ZERO(&exceptfds);

   for(i = 0; i < size; i++)
   {
      Socket *s = eventDesc[i].socket;

      eventDesc[i].eventFlags = 0;

      if(s == NULL || s->fd < 0)
         continue;

      if((eventDesc[i].eventMask & (SOCKET_EVENT_RX_READY | SOCKET_EVENT_ACCEPT)) != 0)
         FD_SET(s->fd, &readfds);
      if((eventDesc[i].eventMask & SOCKET_EVENT_TX_READY) != 0)
         FD_SET(s->fd, &writefds);

      FD_SET(s->fd, &exceptfds);

      if(s->fd > maxfd)
         maxfd = s->fd;
   }

   if(timeout == SHIM_INFINITE_DELAY)
   {
      ptv = NULL;
   }
   else
   {
      tv.tv_sec = timeout / 1000;
      tv.tv_usec = (timeout % 1000) * 1000;
      ptv = &tv;
   }

   r = select(maxfd + 1, &readfds, &writefds, &exceptfds, ptv);

   // Interrupted or timed out: no events, but keep the client's state machine alive.
   if(r < 0)
   {
      if(errno == EINTR)
         return NO_ERROR;
      return ERROR_FAILURE;
   }
   if(r == 0)
      return NO_ERROR;

   for(i = 0; i < size; i++)
   {
      Socket *s = eventDesc[i].socket;
      uint_t f = 0;

      if(s == NULL || s->fd < 0)
         continue;

      if(FD_ISSET(s->fd, &readfds))
         f |= (eventDesc[i].eventMask & (SOCKET_EVENT_RX_READY | SOCKET_EVENT_ACCEPT));
      if(FD_ISSET(s->fd, &writefds))
         f |= (eventDesc[i].eventMask & SOCKET_EVENT_TX_READY);
      // An exception (e.g. peer reset) makes the socket readable-with-error; force a recv so the
      // upper layer observes the error code.
      if(FD_ISSET(s->fd, &exceptfds))
         f |= (eventDesc[i].eventMask & (SOCKET_EVENT_RX_READY | SOCKET_EVENT_TX_READY));

      eventDesc[i].eventFlags = f;
   }

   return NO_ERROR;
}

error_t socketShutdown(Socket *socket, uint_t how)
{
   int h;

   if(socket == NULL || socket->fd < 0)
      return ERROR_INVALID_PARAMETER;

   if(how == SOCKET_SD_RECEIVE)
      h = SHUT_RD;
   else if(how == SOCKET_SD_SEND)
      h = SHUT_WR;
   else
      h = SHUT_RDWR;

   if(shutdown(socket->fd, h) < 0)
   {
      if(errno == ENOTCONN)
         return NO_ERROR;
      return ERROR_FAILURE;
   }
   return NO_ERROR;
}

void socketClose(Socket *socket)
{
   if(socket == NULL)
      return;
   if(socket->fd >= 0)
      close(socket->fd);
   osFreeMem(socket);
}

error_t socketGetRemoteAddr(Socket *socket, IpAddr *ipAddr, uint16_t *port)
{
   struct sockaddr_in sa;
   socklen_t len = sizeof(sa);

   if(socket == NULL || socket->fd < 0 || ipAddr == NULL)
      return ERROR_INVALID_PARAMETER;

   if(getpeername(socket->fd, (struct sockaddr *) &sa, &len) < 0)
      return ERROR_FAILURE;

   ipAddr->length = 4;
   ipAddr->ipv4Addr = sa.sin_addr.s_addr;
   if(port != NULL)
      *port = ntohs(sa.sin_port);

   return NO_ERROR;
}

error_t ipStringToAddr(const char_t *str, IpAddr *ipAddr)
{
   struct in_addr a;

   if(str == NULL || ipAddr == NULL)
      return ERROR_INVALID_PARAMETER;

   if(inet_pton(AF_INET, str, &a) == 1)
   {
      ipAddr->length = 4;
      ipAddr->ipv4Addr = a.s_addr;
      return NO_ERROR;
   }
   return ERROR_FAILURE;
}
