# IKEv2 - frame an IKE_SA_INIT and walk an IKEv2 message

**Layer:** L5 Session · **Build flags:** `PC_ENABLE_IKEV2`

## What this example teaches

**IKEv2** (RFC 7296) is the key-exchange protocol that negotiates **IPsec** security associations - the
standards-track way to build a secure tunnel to a machine over an untrusted network, over UDP 500 / 4500. `services/ikev2` is **tier 1** of that stack: the pure wire codec. It builds and parses the
28-octet IKE header and the payload chain (SA, KE, Nonce, IDi/IDr, AUTH, Notify, Delete, Traffic
Selectors, and the SK encrypted envelope) into fixed caller buffers, with no heap.

> **Scope.** This tier does the **framing only**. The Diffie-Hellman math, the SKEYSEED / SK\_\* key
> derivation, the SK AEAD, and the IKE_SA_INIT -> IKE_AUTH state machine are later tiers (they reuse the
> curve25519 / AES-GCM / HMAC the library already ships). So the KE public value and nonce in this
> sketch are **placeholders** - it emits a structurally valid IKE_SA_INIT and can walk any IKEv2
> message, but it does not complete a handshake.

An IKEv2 message is a fixed header then a forward-linked chain of payloads, each with a 4-byte generic
header (next-payload + critical bit + length):

```cpp
IkeHeader h = { .next_payload = IkePayloadType::IKE_PL_SA, .version = PC_IKE_VERSION, .exchange = IkeExchange::IKE_SA_INIT, ... };
size_t off  = pc_ike_hdr_build(buf, cap, &h);
IkeTransform tr[] = { {IkeTransformType::IKE_TRANSFORM_ENCR, IKE_ENCR_AES_CBC, 256}, {IkeTransformType::IKE_TRANSFORM_DH, IKE_DH_MODP2048, -1}, ... };
off += pc_ike_sa_build(buf + off, cap - off, IkePayloadType::IKE_PL_KE, 1, IkeProtocol::IKE_PROTO_IKE, nullptr, 0, tr, 4);
off += pc_ike_ke_build(buf + off, cap - off, IkePayloadType::IKE_PL_NONCE, IKE_DH_MODP2048, ke_pub, ke_len);
off += pc_ike_nonce_build(buf + off, cap - off, IkePayloadType::IKE_PL_NONE, nonce, nonce_len);
pc_ike_set_length(buf, cap, off);   // backfill the whole-message length

// walk any incoming message
IkePayloadIter it;  pc_ike_payload_iter_init(&it, h.next_payload, buf + PC_IKE_HDR_LEN, off - PC_IKE_HDR_LEN);
IkePayload pl;  while (pc_ike_payload_next(&it, &pl)) { /* pl.type, pl.body, pl.body_len */ }
```

The sketch builds an IKE_SA_INIT, hexdumps it, and walks its own payload chain. If you set `GATEWAY_IP`
it also fires the message at that gateway on UDP 500 and parses whatever comes back - a real gateway
usually answers a well-formed but crypto-incomplete IKE_SA_INIT with an `INVALID_KE_PAYLOAD` or
`COOKIE` **Notify**, which the codec parses and prints (proof the parser handles a real peer's framing).

## Prerequisites

Nothing is required for the build + self-parse output. To see a real reply, point `GATEWAY_IP` at an
IKEv2 responder (e.g. a strongSwan / libreswan host, or a router's IPsec endpoint) reachable on UDP 500.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_IKEV2=1" \
  --lib="." examples/L5-Session/IKEv2/IKEv2.ino
```

Flash and watch Serial @ 115200:

```
IP: 192.168.1.42
[ike] built IkeExchange::IKE_SA_INIT, 92 bytes:
11 22 ...
[ike] self: exch=34 flags=0x08 msgid=0 len=92
[ike]   payload type=33 body=... (SA proposal 1, 4 transforms)
[ike]   payload type=34 body=... (KE group 14, 32 bytes)
[ike]   payload type=40 body=16
[ike] no GATEWAY_IP set - build + self-parse only
[ike] done
```

## Annotated source

The complete sketch is [IKEv2.ino](IKEv2.ino). The codec itself is in
[src/services/security/ikev2/ikev2.h](../../../src/services/security/ikev2/ikev2.h); the header, payload chain, and the
SA -> proposal -> transform tree (including the key-length attribute) are verified against RFC 7296 +
the IANA registry and cross-checked byte-for-byte against scapy's IKEv2 codec.
