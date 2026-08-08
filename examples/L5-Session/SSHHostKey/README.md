# SSHHostKey - provision an SSH host key

**Layer:** L5 Session · **Build flags:** `PC_ENABLE_SSH`

## What this example teaches

A server's **host key** is what proves to a client that it is really talking to
this device: the server signs the key exchange with it, and the client checks
that signature against the key it trusts (its `known_hosts`). This example shows
the two ways to give the server a host key, both starting from a key pair you
generate on your workstation - never a key committed in the repo.

### Step 1 - generate a key pair

Use the bundled generator (needs `openssl` and `ssh-keygen`):

```sh
# Ed25519 (for the compile-time path) - writes a C header you embed:
python3 tools/crypto/gen_ssh_host_key.py --type ed25519 \
    --header examples/L5-Session/SSHHostKey/host_key.h --symbol HOST_KEY_SEED

# RSA-2048 (for the NVS path) - writes ssh_host_key.der to store on the device:
python3 tools/crypto/gen_ssh_host_key.py --type rsa --out-dir ./keys
```

Each run prints a `.pub` line. Trust it on your client:

```sh
echo "<board-ip> $(cat keys/ssh_host_key.pub)" >> ~/.ssh/known_hosts
```

> The private key is **not** committed and never leaves your machine. Keep it
> secret; anyone with it can impersonate your server.

### Step 2 - choose how to install it (`HOST_KEY_PROVISION` in the sketch)

**`1` - at compile time (default).** The 32-byte Ed25519 seed from `host_key.h`
is compiled into the firmware and installed in one call:

```cpp
#include "host_key.h"            // HOST_KEY_SEED[32], generated in step 1
pc_ssh_hostkey_ed25519_set(HOST_KEY_SEED);
```

Simplest to get running; the key ships inside the image. Good for a fleet you
flash yourself.

**`2` - as a runtime service (NVS).** The RSA key lives in the device's NVS
(namespace `ssh_host_key`, key `priv_der`). Write it **once**, then every boot
just loads its public half - the private key is read per-signature into a stack
buffer and wiped, so it is never held in RAM:

```cpp
// one-time, built with -DPROVISION_WRITE_ONCE and #include "host_key_der.h"
// (gen_ssh_host_key.py --type rsa --header host_key_der.h --symbol HOST_KEY_DER):
Preferences p; p.begin("ssh_host_key", false);
p.putBytes("priv_der", HOST_KEY_DER, HOST_KEY_DER_LEN);
p.end();
// every boot after that:
pc_ssh_rsa_load_pubkey();           // loads only the public half
```

This keeps the key out of the firmware image and lets you rotate it without a
rebuild (a provisioning portal, a factory step, an OTA-delivered blob). See
`docs/SSH.md` "Host key provisioning".

> The sketch has an **inline public demo seed** so it builds out of the box. It
> authenticates nothing. Generating a key in step 1 writes `host_key.h`, which the
> sketch picks up automatically (`__has_include`) and uses instead - it is
> git-ignored so your private key never lands in a commit.

## Build and run

`PC_ENABLE_SSH` must reach the library build, so pass it as a build flag:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_SSH=1" \
  --lib="." examples/L5-Session/SSHHostKey/SSHHostKey.ino
```

Then connect (the demo accepts password auth for `admin`):

```sh
ssh -p 22 admin@<ip>     # password: s3cret  (type; the server echoes it back)
```

If your client warns about a changed host key, it is because you regenerated the
key in step 1 - update the `known_hosts` line to the new `.pub`.
