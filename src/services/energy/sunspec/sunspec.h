// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sunspec.h
 * @brief SunSpec Modbus device-information-model codec (PC_ENABLE_SUNSPEC) - zero-heap
 *        model-chain walker + register-point readers and a map builder, layered on the
 *        holding-register model so a solar inverter / meter / battery is interoperable.
 *
 * A SunSpec map (SunSpec Device Information Model) lives in a contiguous holding-register
 * block and is laid out as raw big-endian 16-bit registers:
 * @code
 *   "SunS" (0x53756E53, 2 registers)        // well-known identifier
 *   [Model ID][Length L][L body registers]  // repeated, one per model
 *   ...
 *   [0xFFFF][0]                             // end model terminates the map
 * @endcode
 * Length L is the number of registers after the length point (the model body). Models are
 * contiguous (no gap between them). Common model = ID 1.
 *
 * The reader is a cursor over the received register bytes (big-endian, 2 bytes/register):
 * verify the marker, then walk each model and read typed points by register offset. The
 * writer emits the same layout (marker, model headers + points, end model) for a device
 * exposing its own map. Marker / header format verified against the SunSpec spec.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SUNSPEC_H
#define PROTOCORE_SUNSPEC_H

#include "protocore_config.h"

#if PC_ENABLE_SUNSPEC

#define SUNSPEC_MARKER 0x53756E53u ///< "SunS"
#define SUNSPEC_END_MODEL 0xFFFFu  ///< end-model id
#define SUNSPEC_COMMON_MODEL 1     ///< common model id

/** @brief One model located in the register map. @ref body points INTO the source buffer. */
typedef struct
{
    uint16_t id;
    uint16_t length;     ///< body registers (after the length point)
    const uint8_t *body; ///< model body, big-endian (id+length header excluded)
    size_t body_len;     ///< length * 2 bytes
} SunSpecModel;

// ---- reader ----

/** @brief True if the SunS identifier (0x53756E53) is at the head of @p regs. */
proto_bool pc_sunspec_check_marker(const uint8_t *regs, size_t len);

/** @brief Begin a walk: verifies the marker and sets *offset just past it (to 4). */
proto_bool pc_sunspec_begin(const uint8_t *regs, size_t len, size_t *offset);

/**
 * @brief Read the model at *offset and advance past it.
 * @return true and fills @p out for a model; false at the end model (0xFFFF) or on truncation.
 */
proto_bool pc_sunspec_next_model(const uint8_t *regs, size_t len, size_t *offset, SunSpecModel *out);

// Typed point readers at a register offset within a model body (big-endian).
uint16_t pc_sunspec_u16(const uint8_t *body, size_t reg);
int16_t pc_sunspec_i16(const uint8_t *body, size_t reg);
uint32_t pc_sunspec_u32(const uint8_t *body, size_t reg);
int32_t pc_sunspec_i32(const uint8_t *body, size_t reg);

/**
 * @brief Copy a SunSpec string point (@p nregs registers, NUL-padded) into @p out.
 * @return true on success (NUL-terminated, content up to the first NUL), false on bad args.
 */
proto_bool pc_sunspec_string(const uint8_t *body, size_t reg, size_t nregs, char *out, size_t out_cap);

// ---- writer ----

/** @brief Cursor for building a SunSpec map. Treat the fields as opaque. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    size_t pos;
    proto_bool error;
} SunSpecWriter;

void pc_sunspec_writer_init(SunSpecWriter *w, uint8_t *buf, size_t cap);
proto_bool pc_sunspec_write_marker(SunSpecWriter *w); ///< "SunS"
proto_bool pc_sunspec_write_model_header(SunSpecWriter *w, uint16_t id, uint16_t length);
proto_bool pc_sunspec_write_u16(SunSpecWriter *w, uint16_t v);
proto_bool pc_sunspec_write_i16(SunSpecWriter *w, int16_t v);
proto_bool pc_sunspec_write_u32(SunSpecWriter *w, uint32_t v);
proto_bool pc_sunspec_write_i32(SunSpecWriter *w, int32_t v);
proto_bool pc_sunspec_write_string(SunSpecWriter *w, const char *s, size_t nregs); ///< nregs registers, NUL-padded
proto_bool pc_sunspec_write_end_model(SunSpecWriter *w);                           ///< [0xFFFF][0]
size_t pc_sunspec_writer_finish(SunSpecWriter *w);                                 ///< bytes written, or 0 on overflow

#endif // PC_ENABLE_SUNSPEC

#endif // PROTOCORE_SUNSPEC_H
