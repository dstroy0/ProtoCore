# SSH-2 interop: pc SSH server ⇄ CycloneSSH

Real-peer conformance test for the library's SSH server against a **second, independent** SSH stack:
[Oryx Embedded **CycloneSSH**](https://github.com/Oryx-Embedded/CycloneSSH). It complements the OpenSSH
peer ([`test/servers/peers/ssh_peer.py`](../peers/ssh_peer.py)) - two unrelated from-scratch SSH
implementations both completing a full handshake + auth + channel exchange with our server is far
stronger evidence than either alone.

**This peer found a real bug.** CycloneSSH could not complete a handshake with our SSH server - the
server reset the connection after `KEX_ECDH_INIT`. Root cause: our server negotiated algorithms by
**its own preference**, but RFC 4253 §7.1 mandates **client preference**; with a client whose order
differs from ours, the two sides picked different algorithms and the KEX init sizes disagreed. OpenSSH
advertises the same PQC/curve-first order we do, so its 16/16 matrix never exposed it. Fixed (see
[docs/BUGS.md](../../../docs/BUGS.md)) and HW-verified below.

## What it is

[`cyclone_ssh_client.c`](cyclone_ssh_client.c) is a CycloneSSH SSH-2 client. Because CycloneSSH binds
to CycloneTCP's socket type (no callback seam like CycloneSSL), it is driven over a small **POSIX
socket shim** ([`socket.c`](socket.c) + [`shim/core/net.h`](shim/core/net.h)) that implements the
CycloneTCP socket surface (`socketOpenEx`/`socketConnect`/`socketSend`/`socketReceive`/
`socketSetTimeout`/`socketPoll`/`socketClose`) over BSD sockets - so no TCP/IP stack is linked. It
negotiates curve25519-sha256 + ssh-ed25519 + chacha20-poly1305 (or aes256-gcm), authenticates
(public key or password), opens a session channel, and round-trips a token over it.

## Run it

Linux only. The Oryx Cyclone sources (GPLv2) are cloned at run time into `.work/`, never vendored.

**Self-test (default) - localhost OpenSSH `sshd`, proves the client is functional:**

```sh
test/servers/cyclone_ssh/run_interop.sh
# >> PASS [public key]
# >> ALL PASS
```

**Against our SSH server on a device rig (the real interop):**

```sh
CYCLONE_SSH_RIG_HOST=192.168.1.153 CYCLONE_SSH_RIG_USER=admin CYCLONE_SSH_RIG_PASS=s3cret \
  test/servers/cyclone_ssh/run_interop.sh
```

Our server echoes channel data (it does not execute commands), so the client uses `--echo` (write a
token, read it back).

## HW-verified

Verified on an **ESP32-P4** running the library's SSH server (Ethernet, `192.168.1.153:22`,
ssh-ed25519 host key, password `admin`/`s3cret`, echo channel). The CycloneSSH client completes:

```
Selected kex algo = curve25519-sha256
Selected server host key algo = ssh-ed25519
Selected client enc algo = chacha20-poly1305@openssh.com
SSH_MSG_KEX_ECDH_REPLY message received (179 bytes)...
[client] server host key presented (51 bytes) -- accepted (interop)
SSH_MSG_USERAUTH_SUCCESS message received (1 bytes)...
[client] SSH connection established (KEX + auth OK)
[client] writing token over the channel: cyclone-interop-...
[client] interop OK: got expected token over the SSH channel
```

i.e. a full KEX + host-key verify + password auth + a **byte-exact encrypted-channel round-trip** on
real silicon - and OpenSSH is unaffected by the fix.
