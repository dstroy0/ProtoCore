# SSHSftp - SFTP (and SCP) file server over SSH

This sketch serves files from a filesystem **over the one authenticated SSH
port**: a client's `sftp` session reads/writes/lists files, and `scp` drops a
file onto the device. It is the standards-track "southbound" path for pushing
files - e.g. NC / G-code programs - onto the device securely, alongside a shell
on the same port.

It is the SSH server example ([SSH](../SSH/)) plus **four calls**: mount a
filesystem behind the mount backend (`pc_mnt_mount(pc_mnt_fs(&LittleFS))`), set
the root once (`pc_fs_begin("/")`), then start the two servers
(`pc_ssh_sftp_begin()` and `pc_ssh_scp_begin()`). The SFTP subsystem and the SCP
exec attach to the existing SSH channel layer.

The root is set once for the device rather than once per protocol, because where
the volume begins is a property of what is mounted - two servers over the same
storage cannot disagree about it.

## What you get

- **SFTP v3** (the OpenSSH `sftp` client's default): `put` / `get` / `ls` /
  `mkdir` / `rmdir` / `rm` / `rename` / `stat` / `realpath`, with streamed
  reads/writes (no heap beyond the fs layer's file handle) and a fixed handle
  table (`PC_SFTP_MAX_HANDLES`). Every path is checked for `..` traversal
  before touching the filesystem.
- **SCP upload** (`scp localfile admin@<ip>:/path`): the rcp SINK direction, one
  file per transfer. (Download - `scp <ip>:/path localfile` - is a follow-up; use
  `sftp get` for now.)

## What you will need

- An ESP32 board with a filesystem: this sketch mounts **LittleFS** (formats on
  first boot); an **SD card** works too - open it with `SD.begin(...)` and pass
  `SD` instead of `LittleFS`.
- An SSH **host key** provisioned in NVS (see `docs/SSH.md`, "Host key
  provisioning") - the sketch loads it with `pc_ssh_rsa_load_pubkey()`.

## Configure + connect

Edit `SSID` / `PASSWORD` (and the demo password in `ssh_password_auth`), flash,
and open Serial @ **115200**. Then, from a machine with the OpenSSH client:

```bash
sftp -P 22 admin@<board-ip>        # password: s3cret
sftp> put firmware.bin /fw.bin     # upload
sftp> ls -l /                      # list
sftp> get /fw.bin copy.bin         # download (byte-exact)
sftp> mkdir /gcode
sftp> rename /fw.bin /gcode/fw.bin
sftp> rm /gcode/fw.bin

scp -P 22 part.nc admin@<board-ip>:/gcode/part.nc   # upload via scp
```

## Troubleshooting

- **`Permission denied`.** The demo accepts `admin` / `s3cret` (password auth).
  Force password auth if your client prefers keys:
  `sftp -o PreferredAuthentications=password -o PubkeyAuthentication=no ...`.
- **`No SSH host key in NVS`.** Provision the host key first (`docs/SSH.md`).
- **A big `put`/`scp` prints a broken-pipe / non-zero exit at the end but the
  file is correct.** That is the SSH server's non-graceful connection teardown
  (a plain `ssh <host> exit` shows it too, with exit 0). The transfer itself is
  byte-exact; it is a documented follow-up (a graceful `SSH_MSG_DISCONNECT`).
- **`scp <ip>:/path local` hangs / "not supported".** Download over SCP is not
  implemented yet - use `sftp get`.

## Going further

- **Bigger transfers.** SFTP `READ` returns a short `DATA` bounded by one SSH
  packet (`SSH_PKT_BUF_SIZE`); raise `SSH_PKT_BUF_SIZE` **and** `PC_SFTP_MAX_READ`
  for higher throughput (`SSH_CHAN_MAX_PACKET` derives from `SSH_PKT_BUF_SIZE`).
- **Restrict the mount.** Set a subdirectory as the root (e.g.
  `pc_fs_begin("/gcode")`) so a client cannot see the whole volume; the `..`
  guard keeps requests inside it. A trailing slash is not required - the
  accessor appends one, so `"/gcode"` and `"/gcode/"` mean the same thing.
- **Machine-tool push.** Combine with `services/dnc` to drip a pushed `.nc`
  program to an attached controller over RS-232 / a socket.

## Build and run (PlatformIO)

The SFTP/SCP server lives inside the library, so the flags must reach the whole
build:

```bash
pio ci examples/L5-Session/SSHSftp \
  --board esp32dev \
  --lib "." \
  --project-option="build_flags=-DPC_ENABLE_SSH=1 -DPC_ENABLE_SSH_SFTP=1 -DPC_ENABLE_SSH_SCP=1 -DPC_ENABLE_MNT=1"
```

(The Arduino IDE reads the flags from `build_opt.h` beside the sketch automatically.)

---

## How it works under the hood (for the curious)

An SSH `CHANNEL_REQUEST` of type `subsystem`/`sftp` (or `exec "scp …"`) is
recognized in the channel layer, which tags the channel and routes its data to
the SFTP/SCP binding instead of the shell echo. The binding accumulates the
channel byte stream into `SSH_FXP_*` (or rcp) records, executes each against the
`fs::FS` mount - reusing the same file operations WebDAV and the static file
server use - and frames responses back with `pc_ssh_conn_send`. A large SFTP `WRITE`
(or an SCP upload) is streamed straight to the file as the fragments arrive, so a
transfer is never bounded by a buffer. The wire codecs (`services/sftp`,
`services/scp`) are pure and host-tested (`native_ssh_sftp`, `native_scp`); this
glue binds their seams to the SSH channel + `fs::FS`. HW-verified against the
OpenSSH `sftp`/`scp` clients on an ESP32-S3 with an SD card (byte-exact round
trips).
