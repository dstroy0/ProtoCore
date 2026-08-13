// TREATMENT: pimpl + composition, slot index only (no public struct).
// The header names the types and refuses to describe them. A caller holds a uint8_t slot and can
// reach nothing except through these calls, whose bodies live in opq.c.
#ifndef OPQ_H
#define OPQ_H

#include <stddef.h>
#include <stdint.h>

#define RT_CAP 1024u
#define RT_SLOTS 8u

struct RtInternal; // opaque: state + cursors
struct RtStorage;  // opaque: the bulk ring backing

size_t rt_available(uint8_t slot);
int rt_read_byte(uint8_t slot, uint8_t *out);
void rt_produce(uint8_t slot, const uint8_t *src, size_t n);

#endif
