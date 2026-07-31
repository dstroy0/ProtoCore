// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Test tooling only, NOT part of the library; builds against Oryx CycloneSSH/CycloneCRYPTO
// (GPLv2), fetched at run time by run_interop.sh - never vendored here.
// CycloneSSH SSH-2 interop client (host Linux).
//
// Completes a real SSH-2 handshake against a reference server (OpenSSH sshd): version exchange,
// curve25519-sha256 (or ecdh-sha2-nistp256) key exchange, ssh-ed25519 host-key verify,
// chacha20-poly1305 / aes*-gcm cipher, then user authentication (password or public key), then
// opens a "session" channel, writes a token over it and reads the echo (the test server echoes
// channel data). Exit 0 iff the expected token comes back over the encrypted channel.
//
// The heavy lifting is CycloneSSH's own shell client (shell/shell_client.c) driving the SSH
// engine; this file only seeds the PRNG, accepts the host key (interop, not a trust decision),
// supplies the credentials, and drives the shell-client FSM. All socket I/O goes through the
// POSIX socket shim in socket.c (core/net.h), so no CycloneTCP is linked.
//
//   usage: cyclone_ssh_client <host> <port> <user> <password> [--echo]
//          cyclone_ssh_client <host> <port> <user> --key <privkey_file> <pubkey_file>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ssh/ssh.h"
#include "shell/shell_client.h"
#include "rng/hmac_drbg.h"
#include "hash/sha256.h"

// PRNG required by CycloneSSH (ephemeral KEX key, padding, etc.).
static HmacDrbgContext g_drbg;

// Credentials / auth material captured from argv.
static const char *g_user = NULL;
static const char *g_password = NULL; // non-NULL => password auth
static char *g_privkey = NULL;        // non-NULL => public-key auth
static size_t g_privkey_len = 0;
static char *g_pubkey = NULL;
static int g_echo_mode = 0; // --echo: write a token + read the echo (our server) vs exec+read (OpenSSH)
static size_t g_pubkey_len = 0;

// Accept whatever host key the server presents. This is an interop test, not a trust decision;
// the exchange-hash signature over the host key is still verified by CycloneSSH, so the peer is
// still cryptographically authenticated as the holder of that key.
static error_t host_key_verify_cb(SshConnection *connection, const uint8_t *hostKey,
   size_t hostKeyLen)
{
   (void) connection;
   (void) hostKey;
   fprintf(stderr, "[client] server host key presented (%zu bytes) -- accepted (interop)\n",
      hostKeyLen);
   return NO_ERROR;
}

// Invoked by shellClientOpenConnection right after sshInit, before the socket is opened. This is
// where the SSH context is wired up: PRNG, host-key policy, username, and credentials.
static error_t ssh_init_cb(ShellClientContext *context, SshContext *sshContext)
{
   error_t error;

   (void) context;

   error = sshSetPrng(sshContext, HMAC_DRBG_PRNG_ALGO, &g_drbg);
   if(error)
      return error;

   error = sshRegisterHostKeyVerifyCallback(sshContext, host_key_verify_cb);
   if(error)
      return error;

   error = sshSetUsername(sshContext, g_user);
   if(error)
      return error;

   if(g_password != NULL)
   {
      // Password authentication.
      error = sshSetPassword(sshContext, g_password);
      if(error)
         return error;
   }
   else
   {
      // Public-key authentication: the client's key pair is loaded into the host-key store
      // (slot 0), which the client-side "publickey" method signs with.
      error = sshLoadHostKey(sshContext, 0, g_pubkey, g_pubkey_len, g_privkey,
         g_privkey_len, NULL);
      if(error)
         return error;
   }

   return NO_ERROR;
}

static int seed_prng(void)
{
   uint8_t seed[48];
   FILE *f = fopen("/dev/urandom", "rb");

   if(f == NULL || fread(seed, 1, sizeof(seed), f) != sizeof(seed))
   {
      fprintf(stderr, "cannot read /dev/urandom\n");
      if(f != NULL)
         fclose(f);
      return -1;
   }
   fclose(f);

   if(hmacDrbgInit(&g_drbg, SHA256_HASH_ALGO) != NO_ERROR ||
      hmacDrbgSeed(&g_drbg, seed, sizeof(seed)) != NO_ERROR)
   {
      fprintf(stderr, "PRNG seed failed\n");
      return -1;
   }
   return 0;
}

static char *read_file(const char *path, size_t *len)
{
   FILE *f = fopen(path, "rb");
   long sz;
   char *buf;

   if(f == NULL)
      return NULL;
   if(fseek(f, 0, SEEK_END) != 0)
   {
      fclose(f);
      return NULL;
   }
   sz = ftell(f);
   if(sz < 0)
   {
      fclose(f);
      return NULL;
   }
   rewind(f);

   buf = malloc((size_t) sz + 1);
   if(buf == NULL)
   {
      fclose(f);
      return NULL;
   }
   if(fread(buf, 1, (size_t) sz, f) != (size_t) sz)
   {
      free(buf);
      fclose(f);
      return NULL;
   }
   fclose(f);
   buf[sz] = '\0';
   *len = (size_t) sz;
   return buf;
}

// Resolve <host> to an IPv4 IpAddr (network byte order in ipv4Addr).
static int resolve_host(const char *host, IpAddr *addr)
{
   struct in_addr a;
   struct addrinfo hints;
   struct addrinfo *res = NULL;

   if(inet_pton(AF_INET, host, &a) == 1)
   {
      addr->length = 4;
      addr->ipv4Addr = a.s_addr;
      return 0;
   }

   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;

   if(getaddrinfo(host, NULL, &hints, &res) == 0 && res != NULL)
   {
      struct sockaddr_in *sin = (struct sockaddr_in *) res->ai_addr;
      addr->length = 4;
      addr->ipv4Addr = sin->sin_addr.s_addr;
      freeaddrinfo(res);
      return 0;
   }
   return -1;
}

int main(int argc, char **argv)
{
   ShellClientContext ctx;
   IpAddr serverIp;
   error_t error;
   int port;
   char cmd[128];
   char out[2048];
   size_t total = 0;
   int32_t exitStatus;
   int ok;

   if(argc < 4)
   {
      fprintf(stderr, "usage: %s <host> <port> <user> <password>\n", argv[0]);
      fprintf(stderr, "       %s <host> <port> <user> --key <privkey_file> <pubkey_file>\n",
         argv[0]);
      return 2;
   }

   port = atoi(argv[2]);
   g_user = argv[3];
   for(int ai = 4; ai < argc; ai++)
      if(strcmp(argv[ai], "--echo") == 0)
         g_echo_mode = 1;

   if(argc >= 5 && strcmp(argv[4], "--key") == 0)
   {
      if(argc < 7)
      {
         fprintf(stderr, "--key requires <privkey_file> <pubkey_file>\n");
         return 2;
      }
      g_privkey = read_file(argv[5], &g_privkey_len);
      g_pubkey = read_file(argv[6], &g_pubkey_len);
      if(g_privkey == NULL || g_pubkey == NULL)
      {
         fprintf(stderr, "cannot read key files\n");
         return 2;
      }
      fprintf(stderr, "[client] auth method: public key (%s)\n", argv[6]);
   }
   else if(argc >= 5)
   {
      g_password = argv[4];
      fprintf(stderr, "[client] auth method: password\n");
   }
   else
   {
      fprintf(stderr, "no password or key supplied\n");
      return 2;
   }

   if(resolve_host(argv[1], &serverIp) != 0)
   {
      fprintf(stderr, "cannot resolve host %s\n", argv[1]);
      return 2;
   }

   if(seed_prng() != 0)
      return 2;

   // --- shell client setup ---
   error = shellClientInit(&ctx);
   if(error)
   {
      fprintf(stderr, "shellClientInit failed: %d\n", error);
      return 2;
   }

   error = shellClientRegisterSshInitCallback(&ctx, ssh_init_cb);
   if(error)
   {
      fprintf(stderr, "shellClientRegisterSshInitCallback failed: %d\n", error);
      return 2;
   }

   shellClientSetTimeout(&ctx, 15000);

   // --- connect: TCP connect + SSH KEX + user authentication ---
   fprintf(stderr, "[client] connecting to %s:%d as '%s'\n", argv[1], port, g_user);
   error = shellClientConnect(&ctx, &serverIp, (uint16_t) port);
   if(error)
   {
      fprintf(stderr, "[client] shellClientConnect FAILED: error=%d\n", error);
      shellClientDeinit(&ctx);
      return 1;
   }
   fprintf(stderr, "[client] SSH connection established (KEX + auth OK)\n");

   // --- open a session channel and run the exchange (mode-dependent) ---
   // default: exec "echo <token>" and read the command stdout (an OpenSSH sshd runs the command).
   // --echo : exec, then WRITE the token and read it back (our test server echoes channel DATA and
   //          does not execute commands).
   char token[64];
   snprintf(token, sizeof(token), "cyclone-interop-%ld", (long) getpid());
   if(g_echo_mode)
      snprintf(cmd, sizeof(cmd), "echo");
   else
      snprintf(cmd, sizeof(cmd), "echo %s", token);
   fprintf(stderr, "[client] exec: %s%s\n", cmd, g_echo_mode ? " (echo mode: will write the token)" : "");
   error = shellClientExecuteCommand(&ctx, cmd);
   if(error)
   {
      fprintf(stderr, "[client] shellClientExecuteCommand FAILED: error=%d\n", error);
      shellClientDisconnect(&ctx);
      shellClientDeinit(&ctx);
      return 1;
   }

   if(g_echo_mode)
   {
      size_t written = 0;
      char line[80];
      snprintf(line, sizeof(line), "%s\n", token);
      error = shellClientWriteStream(&ctx, line, strlen(line), &written, 0);
      if(error)
      {
         fprintf(stderr, "[client] shellClientWriteStream FAILED: error=%d\n", error);
         shellClientDisconnect(&ctx);
         shellClientDeinit(&ctx);
         return 1;
      }
      shellClientFlushStream(&ctx);
   }

   // --- read the token back (echoed data, or the command stdout) ---
   out[0] = '\0';
   error = shellClientReadStream(&ctx, out, sizeof(out) - 1, &total, 0);
   if(error != NO_ERROR && error != ERROR_END_OF_STREAM)
   {
      fprintf(stderr, "[client] shellClientReadStream FAILED: error=%d\n", error);
      shellClientDisconnect(&ctx);
      shellClientDeinit(&ctx);
      return 1;
   }
   out[total] = '\0';

   // --- drain remaining output + close the channel, then collect exit status ---
   shellClientCloseStream(&ctx);
   exitStatus = shellClientGetExitStatus(&ctx);

   // --- print the command output on stdout (the deliverable proof) ---
   printf("%s", out);
   fflush(stdout);

   fprintf(stderr, "[client] command exit status = %d\n", (int) exitStatus);

   // --- graceful disconnect ---
   shellClientDisconnect(&ctx);
   shellClientDeinit(&ctx);

   if(g_privkey != NULL)
      free(g_privkey);
   if(g_pubkey != NULL)
      free(g_pubkey);

   ok = (strstr(out, "cyclone-interop-") != NULL);
   if(!ok)
   {
      fprintf(stderr, "[client] expected token NOT found in channel output\n");
      return 1;
   }
   fprintf(stderr, "[client] interop OK: got expected token over the SSH channel\n");
   return 0;
}
