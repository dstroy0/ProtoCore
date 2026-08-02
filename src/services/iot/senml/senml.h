// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file senml.h
 * @brief SenML (RFC 8428) sensor-measurement pack builder (PC_ENABLE_SENML) - zero-heap
 *        SenML-JSON and SenML-CBOR encoders over the shipped JSON / CBOR codecs. SenML is
 *        the standard measurement format for CoAP / LwM2M / HTTP telemetry.
 *
 * A SenML pack is an array of records. Each record carries an optional base name / base time
 * (applied to the records that follow), a name, a unit, and exactly one value (a number, a
 * string, or a boolean), plus an optional time:
 * @code
 *   [{"bn":"urn:dev:ow:10e2073a01080063","u":"Cel","v":23.1}]
 * @endcode
 * SenML-JSON uses the text labels (bn/bt/n/u/v/vs/vb/t); SenML-CBOR uses the integer labels
 * (n=0 u=1 v=2 vs=3 vb=4 t=6, bn=-2 bt=-3) in a CBOR map per record. Numbers that are
 * integral are emitted as integers (so timestamps keep full precision); otherwise as floats.
 *
 * The caller fills a @ref SenmlRecord array and the builder emits the whole pack into a
 * caller buffer (fail-closed on overflow). Verified against the RFC 8428 example. A resolver
 * (@ref pc_senml_resolve, RFC 8428 §4.6) folds the base name / base time into each record so a
 * consumer gets standalone records with a full name and an absolute time.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SENML_H
#define PROTOCORE_SENML_H

#include "network_drivers/presentation/codec/codec.h" // pc_codec - the encoding is a parameter
#include "protocore_config.h"

#if PC_ENABLE_SENML

/** @brief Which value field a record carries. */
typedef enum PROTO_ENUM_PACKED
{
    SENML_V_NONE,   ///< no value (e.g. a base-only record)
    SENML_V_FLOAT,  ///< numeric value (v) - emitted as int when integral, else float
    SENML_V_STRING, ///< string value (vs)
    SENML_V_BOOL    ///< boolean value (vb)
} SenmlValueKind;

/** @brief One SenML record. String pointers are borrowed (not copied); nullptr fields are skipped. */
typedef struct
{
    const char *base_name; ///< bn (optional)
    proto_bool has_base_time;
    double base_time; ///< bt (optional)
    const char *name; ///< n (optional)
    const char *unit; ///< u (optional)
    SenmlValueKind value_kind;
    double value;          ///< v (when value_kind == SENML_V_FLOAT)
    const char *value_str; ///< vs (when value_kind == SENML_V_STRING)
    proto_bool value_bool; ///< vb (when value_kind == SENML_V_BOOL)
    proto_bool has_time;
    double time; ///< t (optional)
} SenmlRecord;

/** @brief Build a SenML-JSON pack. Returns bytes written (excluding NUL), or 0 on overflow. */
size_t pc_senml_json_build(char *buf, size_t cap, const SenmlRecord *records, size_t count);

/** @brief Build a SenML-CBOR pack (a CBOR array of integer-keyed maps). Returns bytes, or 0. */
/**
 * @brief Build a SenML pack in whichever binary encoding @p c is.
 *
 * The RFC 8428 integer labels and the record structure are the same whatever carries them, so the
 * encoding is a parameter rather than a second copy of this function: pass pc_codec_cbor() for
 * SenML-CBOR, pc_codec_msgpack() for the MessagePack pack.
 *
 * @return bytes written, or 0 if the arguments are bad or the buffer was too small.
 */
size_t pc_senml_build(const pc_codec *c, uint8_t *buf, size_t cap, const SenmlRecord *records, size_t count);

// --- resolution (RFC 8428 §4.6): apply the base fields to produce standalone records ---

/** @brief Maximum resolved-name length (base name + name), including the NUL. */
#define SENML_RESOLVED_NAME_MAX 96

/** @brief A resolved SenML record: the base name / time folded in, so it stands alone. */
typedef struct
{
    char name[SENML_RESOLVED_NAME_MAX]; ///< the base name concatenated with the record name
    const char *unit;                   ///< u (borrowed from the input; may be null)
    SenmlValueKind value_kind;
    double value;
    const char *value_str;
    proto_bool value_bool;
    proto_bool has_time;
    double time; ///< the base time added to the record time (an absolute time)
} SenmlResolved;

/**
 * @brief Resolve a SenML pack (RFC 8428 §4.6): carry the base name / base time forward across records so
 *        each output record's name is the full base+name and its time is the absolute base+time.
 *
 * A record's base name / base time becomes active for that record and every record after it, until a later
 * record overrides it. Each input record produces one resolved record (a base-only carrier resolves to a
 * value-less record the caller can skip). @return the number of records resolved (min of @p n and @p max).
 */
size_t pc_senml_resolve(const SenmlRecord *in, size_t n, SenmlResolved *out, size_t max);

#endif // PC_ENABLE_SENML

#endif // PROTOCORE_SENML_H
