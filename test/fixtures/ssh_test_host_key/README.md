# SSH test host key (TEST ONLY - INSECURE)

A **known, public, baseline** RSA-2048 SSH host key, committed so the SSH server can
be provisioned for hardware and interop testing with a fixed, reproducible key -
no per-developer key juggling. It is deliberately public (like a known-answer
vector); it authenticates nothing. To provision with a **fresh** key instead, run
`python3 tools/gen_ssh_host_key.py --type rsa` (or `--type ed25519`) and use its
output - see the [`SSHHostKey`](../../../examples/L5-Session/SSHHostKey/) example.

> [!WARNING]
> The private key is committed to this repository in plain sight. It therefore
> provides **no host authentication whatsoever** - anyone with this repo can
> impersonate a server that uses it. **Never use this key in a product.** A real
> deployment generates its own key and keeps it secret (see
> [`docs/SSH.md`](../../../docs/SSH.md) "Host key provisioning").

## Files

| File                    | What it is                                                                                                                          |
| ----------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `ssh_test_host_key.h`   | The private key as PKCS#8 DER, as a C byte array (`PC_SSH_TEST_HOST_KEY_DER` + `..._LEN`) for the one-time NVS provisioning sketch. |
| `ssh_test_host_key.pub` | The matching public key, for a client's `known_hosts`.                                                                              |

The raw `.pem` / `.der` are intentionally **not** committed (PEM private keys trip
GitHub push protection); the byte array carries the same key material in C form.

## Provision a board

```cpp
#include <Preferences.h>
#include "ssh_test_host_key.h"

void setup() {
  Preferences p;
  p.begin("ssh_host_key", false);   // namespace MUST be "ssh_host_key"
  p.putBytes("priv_der", PC_SSH_TEST_HOST_KEY_DER, PC_SSH_TEST_HOST_KEY_DER_LEN);
  p.end();                          // key name MUST be "priv_der"
}
```

Flash that once, then flash the real SSH firmware. On the client, trust the public
key (or accept on first connect):

```sh
echo "<board-ip> $(cat ssh_test_host_key.pub)" >> ~/.ssh/known_hosts
```

## How it was generated

```sh
openssl genrsa 2048 \
  | openssl pkcs8 -topk8 -nocrypt -outform DER \
  | xxd -i            # -> the byte array in ssh_test_host_key.h
ssh-keygen -y -f <key>.pem   # -> ssh_test_host_key.pub
```
