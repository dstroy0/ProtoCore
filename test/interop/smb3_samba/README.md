# SMB 3.1.1 interop (real Samba)

A **manual, on-the-wire interop test** for the SMB2/3 client (`src/services/file_transfer/smb`). It drives the real
`smb_client` engine over a live TCP socket at a real Samba (or Windows) server and confirms a full SMB
3.1.1 session works end to end against a third-party implementation - not just the host mock in
`test/test_smb_client`.

What it proves: dialect **3.1.1** negotiation, the **preauth-integrity** hash chain, the
**SP800-108-derived signing key**, **AES-128-CMAC** message signing, and the **NTLMSSP MIC** all
interoperate. On success the client authenticates, writes a CMAC-signed file the server accepts, and
reads it back byte-exact.

This is not a CI unit test (it needs a running server), so it lives outside the `pio` env matrix.

## 1. Stand up a test Samba (forced to 3.1.1 + mandatory signing)

Any Samba works; to make the test meaningful, force the strongest settings. Unprivileged Samba on a high
port (no root needed) - put this in `smb.conf` (all paths under a scratch `$BASE`):

```ini
[global]
   server min protocol = SMB3_11
   server max protocol = SMB3_11
   server signing = mandatory
   smb ports = 4455
   security = user
   passdb backend = tdbsam:BASE/passdb.tdb
   private dir = BASE/private
   state directory = BASE/state
   cache directory = BASE/cache
   lock directory = BASE/lock
   pid directory = BASE/pid
   ncalrpc dir = BASE/ncalrpc
   log file = BASE/log.smbd
[pcshare]
   path = BASE/share
   read only = no
   valid users = YOURUSER
```

```sh
# create the state dirs, then add the Samba account (non-root, into the private passdb):
printf 'SmbPass123!\nSmbPass123!\n' | pdbedit --configfile="$BASE/smb.conf" -a -u "$USER" -t
# start it (unprivileged, high port):
smbd -s "$BASE/smb.conf" -D
```

## 2. Build the driver (host g++, never on a Windows mount)

Compile `smb_interop.cpp` with the SMB + crypto sources and the native mocks:

```sh
SMB=src/services/smb; CR=src/crypto
g++ -std=c++14 -O1 -DPC_ENABLE_SMB=1 -Isrc -Itest/mocks -o /tmp/smb_interop \
  test/interop/smb3_samba/smb_interop.cpp \
  $SMB/smb2.cpp $SMB/ntlm.cpp $SMB/ntlmssp.cpp $SMB/spnego.cpp $SMB/smb_client.cpp \
  $CR/md.cpp $CR/sha256.cpp $CR/hmac_sha256.cpp $CR/kdf.cpp $CR/aes_cmac.cpp $CR/sha512.cpp
```

## 3. Run

```sh
SMB_HOST=127.0.0.1 SMB_PORT=4455 SMB_USER="$USER" SMB_PASS='SmbPass123!' \
  SMB_SHARE='\\127.0.0.1\pcshare' SMB_PATH=interop.txt /tmp/smb_interop
```

Expected (exit 0):

```
smb_open -> 0
  signing_active=1 algo=AES-CMAC
smb_write -> 0 (74 bytes)
smb_read -> 0 (74 bytes) match=YES
```

`algo=AES-CMAC` + `match=YES` against a `server signing = mandatory` Samba means the CMAC signatures and
the NTLMSSP MIC were accepted by a real server. As a negative check, flipping one byte of the MIC
(`pc_ntlm_mic`) makes Samba reject the logon with `STATUS_LOGON_FAILURE` - confirming the server
actually verifies it. Last verified against **Samba 4.19.5** on 2026-07-25.
