# InterfaceForward - bridge frames between interfaces, DMA-driven

**Layer:** Foundation · **Build flags:** `PC_ENABLE_DMA`,
`PC_ENABLE_PREEMPT_QUEUE`, `PC_ENABLE_FORWARD`, `PC_DMA_SIMULATE`

## What this example teaches

The whole v5 ingest pipeline, end to end. A frame arriving on one interface is
**forwarded** to another, so the device bridges / routes between its interfaces
instead of only terminating traffic:

```
interface A --DMA RX--> callback --post--> FORWARD lane --> pc_forward_ingress()
                                                                  |
                                                 (rule A->B allow, rate-capped)
                                                                  |
                                                       interface B egress send
```

Three pieces snap together, each already its own feature:

- **[DMA](../../Peripherals/DmaIngest/README.md)** moves the bytes off interface A into a static
  buffer and fires a completion.
- The **[FORWARD lane](../PreemptLanes/README.md)** (an internal, high-priority
  preempting lane) carries the frame off the interrupt to a task.
- The **forwarding plane** (`services/forward`) applies the rules and sends the frame
  out interface B.

## The forwarding plane

Register interfaces (each with an egress send callback), then add `src -> dst` rules:

```cpp
pc_forward_add_if(IF_B, pc_if_kind::PC_IF_WIFI_STA, if_b_send, nullptr);
pc_forward_add_rule(IF_A, IF_B, pc_fwd_action::PC_FWD_ALLOW, 0); // 0 = no rate cap

// when a frame arrives on interface A:
pc_forward_ingress(IF_A, bytes, len); // -> forwards to every allowed destination
```

- **Default-deny**: a `(src, dst)` pair forwards only with an ALLOW rule and no DENY
  (a DENY always wins). A frame is never reflected to its source interface.
- **Multi-destination**: add several ALLOW rules for one source to fan a frame out
  (a hub / bridge).
- **Rate cap**: the fourth argument caps an ALLOW rule at N frames/second; excess is
  dropped, not blocked.
- **Fail-closed**: a full destination (send returns false) or an exceeded cap drops
  and is counted (`pc_forward_get_stats()`), never blocks.

### Ingress ACL

Before any forwarding rule runs, an optional **access-control list** filters frames by
content. Each entry matches on the source interface (or `PC_FWD_IF_ANY`) and a byte
pattern under a mask; entries are evaluated in add order, **first match wins**, and a
frame matching none takes the ACL default (permit by default, so the ACL is opt-in):

```cpp
// drop frames whose first byte is 0xFF, on interface A, before forwarding:
uint8_t pat[1] = {0xFF}, mask[1] = {0xFF};
pc_forward_acl_add(IF_A, /*offset*/ 0, pat, mask, /*patlen*/ 1, pc_fwd_action::PC_FWD_DENY);

// allowlist instead (only explicitly permitted frames pass):
pc_forward_acl_set_default(pc_fwd_action::PC_FWD_DENY);
pc_forward_acl_add(IF_A, 0, allowed_hdr, hdr_mask, 2, pc_fwd_action::PC_FWD_ALLOW);
```

This sketch drops every 5th frame (a `0xFF` marker) at the ACL; watch `acl_denied`
climb in the serial stats while `forwarded` counts the rest.

Storage is static (zero heap): `PC_FWD_MAX_IFACES` interfaces,
`PC_FWD_MAX_RULES` rules, `PC_FWD_MAX_ACL` ACL entries.

Here interface A is a DMA channel fed by the simulator (no wire needed) and interface
B's egress just counts the bytes; a real build sends B out Wi-Fi / Ethernet / a bus or
another DMA channel.

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_DMA=1 -DPC_ENABLE_PREEMPT_QUEUE=1 -DPC_ENABLE_FORWARD=1 -DPC_DMA_SIMULATE=1" \
  --lib="." examples/Foundation/InterfaceForward/InterfaceForward.ino
```
