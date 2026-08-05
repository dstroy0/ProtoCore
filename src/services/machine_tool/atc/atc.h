// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file atc.h
 * @brief ATC (Advanced Traffic Controller) field-I/O interop snapshot (PC_ENABLE_ATC).
 *
 * The ATC standard moves traffic cabinets to a standard Linux engine with an ITS-Cabinet / ATC field-I/O
 * API (the FIO): the controller reads detector/input points and drives signal/output points through a
 * fixed I/O map. ATC is a host-platform spec more than a wire protocol, so the useful slice for this
 * library is *interop*: exposing this device's field-I/O (which it already gathers via the shipped NTCIP
 * / NEMA-TS2 / gpio services) to an ATC engine over the existing HTTP surface, as a compact JSON snapshot.
 *
 * This is that snapshot codec: a fixed table of named field-I/O points (each an input or output bit or
 * byte) that `pc_atc_snapshot_json` serializes as `{"inputs":[...],"outputs":[...]}` for a GET, and a
 * setter to drive an output point from an ATC command. Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_ATC_H
#define PROTOCORE_ATC_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_ATC

/** @brief One ATC field-I/O point. */
typedef struct
{
    const char *name;     ///< the FIO point name (borrowed), e.g. "det.1", "phase.2.green".
    proto_bool is_output; ///< true = an output (a driver the controller sets), false = an input (a sensor).
    uint8_t value;        ///< the current 8-bit value (a bit is 0/1; a byte is 0..255).
} AtcPoint;

/** @brief The field-I/O map: a fixed table of points the ATC engine sees. */
typedef struct
{
    AtcPoint *points;
    size_t count;
} AtcFieldIo;

/**
 * @brief Serialize the field-I/O map as `{"inputs":[{"name":..,"value":..},...],"outputs":[...]}`.
 * @return length written (excl NUL), or 0 on overflow / bad args. Point names are JSON-escaped.
 */
size_t pc_atc_snapshot_json(const AtcFieldIo *io, char *out, size_t cap);

/**
 * @brief Drive an output point by name from an ATC command.
 * @return true if the named point exists and is an output (its value is set); false otherwise.
 */
proto_bool pc_atc_set_output(AtcFieldIo *io, const char *name, uint8_t value);

/** @brief Read a point's value by name; @p found (may be null) reports whether it existed. */
uint8_t pc_atc_get(const AtcFieldIo *io, const char *name, proto_bool *found);

#endif // PC_ENABLE_ATC

PROTO_END_DECLS

#endif // PROTOCORE_ATC_H
