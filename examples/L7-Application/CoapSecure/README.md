# CoapSecure - CoAP over DTLS 1.3 (CoAPs)

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_COAP`, `PC_ENABLE_DTLS`

## What this example teaches

This is [CoAP](../CoAP) **secured**. CoAP normally runs as plaintext UDP; on
the open Internet you want `coaps://` - CoAP inside a DTLS 1.3 session (RFC 7252
§9), the datagram counterpart to HTTPS. The same resources (`/info`, `/led`,
`/hello`) are served, but every request and response travels encrypted and
authenticated.

**The resource table is transport-independent.** You register resources exactly
once with `pc_coap_server_add_resource()`; the plaintext server (`pc_coap_server_begin`)
and this secure front-end both dispatch against the same table. Here we bind only
the secure one:

```cpp
pc_coap_server_reset();
pc_coap_server_add_resource("/info", CoapMethodMask::COAP_ALLOW_GET, coap_info);
pc_coap_server_add_resource("/led",  CoapMethodMask::COAP_ALLOW_GET | CoapMethodMask::COAP_ALLOW_PUT, coap_led);
pc_coap_server_add_resource("/hello", CoapMethodMask::COAP_ALLOW_GET, coap_hello);

CoapsServerConfig cfg;
memset(&cfg, 0, sizeof cfg);
cfg.cert_der = COAPS_CERT_DER;              // Ed25519 leaf certificate (DER)
cfg.cert_len = sizeof(COAPS_CERT_DER);
memcpy(cfg.ed25519_seed, COAPS_ED25519_SEED, 32);
esp_fill_random(cfg.cookie_key, 32);        // per-boot HelloRetryRequest cookie secret
cfg.rng = coaps_rng;                        // hardware CSPRNG (esp_fill_random)
pc_coaps_server_begin(PC_COAPS_PORT, &cfg); // UDP 5684
```

**One poll drives everything.** `coaps_server` owns a small pool of DTLS
connections keyed by peer address. `pc_coaps_server_poll()` - called every loop -
routes each queued datagram to its connection, runs the handshake (or decrypts a
CoAP request, answers it, and re-encrypts), fires the DTLS **retransmission timer**
so a lost handshake flight is re-sent (RFC 9147 §5.8), and reaps idle connections:

```cpp
void loop() {
    pc_coaps_server_poll(); // DTLS handshakes + retransmission + idle reaping
    server.handle();     // the TCP server (CoAPs runs off lwIP UDP callbacks + this poll)
}
```

The profile is `TLS_AES_128_GCM_SHA256` + X25519 + an Ed25519 server certificate. A
client that does not offer an X25519 key_share up front is answered with a
**HelloRetryRequest** carrying an address-bound cookie and renegotiates the group,
so any conformant DTLS 1.3 client interoperates.

> **The certificate in the sketch is a throwaway demo key - regenerate your own
> before using this for anything real.** The header comment in
> [CoapSecure.ino](CoapSecure.ino) has the exact `openssl` commands
> (`genpkey -algorithm ed25519` → raw seed + a self-signed DER leaf certificate).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_COAP=1 -DPC_ENABLE_DTLS=1" \
  --lib="." examples/L7-Application/CoapSecure/CoapSecure.ino
```

Then, from a host with a DTLS 1.3 CoAP client (accept the demo certificate):

```sh
coap-client -m get coaps://<ip>/info      # libcoap built with DTLS 1.3
coap-client -m put -e 1 coaps://<ip>/led
aiocoap-client coaps://<ip>/hello         # aiocoap with a DTLS backend
```

The verified reference client is the wolfSSL harness in
[`test/servers/dtls_wolfssl`](../../../test/servers/dtls_wolfssl), which completes
the handshake and an application-data round trip against this same server core.

See also [CoAP](../CoAP) (plaintext), [CoapObserve](../CoapObserve)
(server push), and [CoapBlock](../CoapBlock) (large transfers).
