# SmbFileClient - read a file off a Windows / Samba share

This sketch makes your ESP32 log in to a file server the way a Windows PC does
(SMB2 + NTLMv2), open a file on a shared folder, and print it to the serial
monitor. The motivating use case is a CNC machine controller: the shop keeps its
`.nc` part programs on a file server, and the device pulls the one it needs.

You do not need to know anything about Windows networking. Part 1 walks you,
from nothing, through turning a spare Raspberry Pi (or any Linux box) into a file
server that serves one folder.

## What is going on here? (the big picture)

"SMB" (Server Message Block, also called CIFS) is the protocol Windows uses for
"shared folders" - the `\\server\folder` paths you see in File Explorer. macOS
and Linux speak it too (Linux via **Samba**). It runs over TCP port 445.

To read a file, a client walks a fixed sequence:

1. **NEGOTIATE** - agree on a protocol version (dialect).
2. **SESSION_SETUP** - log in. This library uses **NTLMv2**, the challenge /
   response scheme Windows uses: the server sends a random challenge, the client
   proves it knows the password by hashing it together with that challenge (the
   password itself never crosses the wire). It takes two round trips.
3. **TREE_CONNECT** - pick which share (`\\server\programs`).
4. **CREATE** - open a file in that share; the server hands back a file handle.
5. **READ** / **WRITE** - move the bytes.
6. **CLOSE** - release the handle.

`smb_open()` does steps 1-4 for you and returns an `SmbHandle`; `smb_read()` /
`smb_write()` do step 5; `smb_close()` does step 6.

> **Security note.** NTLM relies on the MD4 and MD5 hashes, which are old and
> cryptographically broken. They are included here only because the NTLM
> handshake requires them on the wire - never use MD4/MD5 for anything new. SMB
> is fine on a trusted LAN; do not expose port 445 to the internet.

## What you will need

- An ESP32 board.
- A file server on the same network. Part 1 sets one up on a Raspberry Pi; if you
  already have a Windows PC with a shared folder, skip to Part 2.
- The server's IP address, a username + password, the share name, and a file in
  it to read.

## Part 1 - Set up a file server (Samba on a Raspberry Pi)

On the Pi (or any Debian/Ubuntu machine):

```bash
sudo apt update
sudo apt install -y samba
```

Make a folder to share and put a test file in it:

```bash
sudo mkdir -p /srv/programs
echo "O0001 (TEST PART)
N10 G21 G90
N20 G0 X0 Y0
N30 M30" | sudo tee /srv/programs/PART001.NC
sudo chmod -R 0777 /srv/programs
```

Add the share to Samba's config. Append this to `/etc/samba/smb.conf`:

```ini
[programs]
   path = /srv/programs
   browseable = yes
   read only = no
   guest ok = no
```

Create a Samba user (this is a **separate** password from the Linux login). Use
an existing Linux account name, e.g. `cnc` - create the Linux user first if
needed with `sudo adduser cnc`:

```bash
sudo smbpasswd -a cnc      # type a password when prompted
sudo systemctl restart smbd
```

Find the Pi's IP address with `hostname -I`, then confirm the share works from
the Pi itself:

```bash
smbclient //localhost/programs -U cnc -c 'ls'
```

You should see `PART001.NC` listed.

## Part 2 - Tell the ESP32 about your server

Open `SmbFileClient.ino` and edit the lines marked **CHANGE ME**:

| Line         | Set it to                                                         |
| ------------ | ----------------------------------------------------------------- |
| `SSID`       | your WiFi network name                                            |
| `PASSWORD`   | your WiFi password                                                |
| `SMB_HOST`   | the server's IP address (e.g. `192.168.1.50`)                     |
| `SMB_USER`   | the Samba username (`cnc` above)                                  |
| `SMB_PASS`   | the Samba password you set with `smbpasswd`                       |
| `SMB_DOMAIN` | empty `""` for a local Samba account; a domain name for AD        |
| `SMB_SHARE`  | the UNC path, e.g. `\\192.168.1.50\programs` (double backslashes) |
| `SMB_PATH`   | the file to read, e.g. `PART001.NC`                               |

## Part 3 - Flash it and watch

Flash the sketch and open Serial Monitor @ **115200**. You should see:

```
Connecting to WiFi....
IP: 192.168.1.77
opened PART001.NC (44 bytes)
--- first 44 bytes ---
O0001 (TEST PART)
N10 G21 G90
N20 G0 X0 Y0
N30 M30
--- end ---
```

## Troubleshooting

The sketch prints `SmbResult` codes; here is what each means.

| Code | Name               | Likely cause                                                                                                |
| ---- | ------------------ | ----------------------------------------------------------------------------------------------------------- |
| `-1` | `SMB_ERR_ARG`      | a `SMB_*` field is empty - check `SMB_USER` / `SMB_SHARE` / `SMB_PATH`                                      |
| `-2` | `SMB_ERR_IO`       | can't reach the server, or it closed the connection (firewall? port 445 open? `sudo systemctl status smbd`) |
| `-3` | `SMB_ERR_PROTOCOL` | share not found (`SMB_SHARE` name), or file not found (`SMB_PATH`), or access denied                        |
| `-4` | `SMB_ERR_AUTH`     | wrong username / password / domain - re-run `smbpasswd -a`                                                  |
| `-5` | `SMB_ERR_OVERFLOW` | a message didn't fit `PC_SMB_BUF`; raise it (see below) for very large paths                                |

"connect failed" (before any `SmbResult`) means the TCP connection never opened -
the server is unreachable on port 445. From another machine, test with
`smbclient //SERVER_IP/programs -U cnc -c ls`.

## Going further

- **Read a bigger file.** `smb_read()` fills your buffer up to its capacity;
  loop it with a growing offset until it returns 0 bytes to stream a whole file.
- **Upload a file.** Open with `desired_access = SMB2_FILE_GENERIC_WRITE` and
  `disposition = SMB2_FILE_OVERWRITE_IF`, then call
  `smb_write(&h, 0, data, len, &wrote, cl_send, cl_recv, &x)`.
- **Throughput.** Each READ / WRITE round trip carries up to `PC_SMB_BUF`
  bytes; raise `PC_SMB_BUF` (default 1024) in `protocore_config.h` for bigger
  transfers, at the cost of stack.

## Build and run (PlatformIO)

SMB lives inside the library, so the flag must reach the whole build:

```bash
pio ci examples/L7-Application/SmbFileClient \
  --board esp32dev \
  --lib "." \
  --project-option="build_flags=-DPC_ENABLE_SMB=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

---

## How it works under the hood (for the curious)

`smb_client` is deliberately transport-agnostic: it moves bytes only through a
**send/recv seam** - two function pointers you supply. That is why the whole
protocol is unit-tested on a PC against a scripted mock server, with no real
network. To run it on a real device you provide the glue that connects the seam
to a socket. In this sketch that glue is `cl_send` / `cl_recv`, which sit on top
of `pc_client`, the library's shared outbound TCP transport:

- `cl_send` writes all the bytes with `Tcp.client->send`.
- `cl_recv` polls `Tcp.client->read` until data arrives, the peer closes, or a
  deadline passes (SMB's messages are length-prefixed, and the engine reads
  exactly one message at a time).

Everything else - the Direct-TCP length framing, building the NTLMv2 response,
wrapping the tokens in SPNEGO, threading the session / tree / file identifiers -
happens inside `smb_open` / `smb_read` / `smb_close`. Point the seam at a
TLS session, a serial link, or a test harness instead, and the same engine runs
unchanged.
