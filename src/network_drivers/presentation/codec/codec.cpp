// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file codec.cpp
 * @brief The wire-encoding tables, one per format, each behind its own feature flag.
 *
 * Every table lives here rather than beside its format, so the set of encodings a build carries is
 * one file to read. A format compiled out contributes no table and no rodata, so a CBOR-only build
 * links the CBOR table alone.
 *
 * Storage is opaque: each table is internal linkage and reachable only through its accessor, so no
 * other translation unit can name it, copy it, or hold a second one. The accessor owns the storage
 * the same way every other owner in the tree does.
 *
 * The initializers are positional: the field order in pc_codec is the operation order, so a member
 * added or moved without the matching change in codec.h fails to compile instead of quietly binding
 * one operation's slot to another's function. That is the whole reason the formats declare their
 * operations in the same order.
 */

#include "network_drivers/presentation/codec/codec.h"
#include "protocore_config.h"
#if PC_NEED_CBOR
#include "network_drivers/presentation/codec/cbor/cbor.h"
#endif
#if PC_ENABLE_MSGPACK
#include "network_drivers/presentation/codec/msgpack/msgpack.h"
#endif

#if PC_NEED_CBOR
static const pc_codec s_cbor = {
    pc_cbor_uint,     pc_cbor_int,       pc_cbor_bytes,     pc_cbor_str,        pc_cbor_str_n,    pc_cbor_bool,
    pc_cbor_null,     pc_cbor_float,     pc_cbor_array,     pc_cbor_map,        pc_cbor_label,

    pc_cbor_peek,     pc_cbor_read_uint, pc_cbor_read_int,  pc_cbor_read_bytes, pc_cbor_read_str, pc_cbor_read_array,
    pc_cbor_read_map, pc_cbor_read_bool, pc_cbor_read_null, pc_cbor_read_float,
};

const pc_codec *pc_codec_cbor(void)
{
    return &s_cbor;
}
#endif // PC_NEED_CBOR

#if PC_ENABLE_MSGPACK
static const pc_codec s_msgpack = {
    pc_msgpack_uint,       pc_msgpack_int,       pc_msgpack_bytes,     pc_msgpack_str,        pc_msgpack_str_n,
    pc_msgpack_bool,       pc_msgpack_null,      pc_msgpack_float,     pc_msgpack_array,      pc_msgpack_map,
    pc_msgpack_label,

    pc_msgpack_peek,       pc_msgpack_read_uint, pc_msgpack_read_int,  pc_msgpack_read_bytes, pc_msgpack_read_str,
    pc_msgpack_read_array, pc_msgpack_read_map,  pc_msgpack_read_bool, pc_msgpack_read_null,  pc_msgpack_read_float,
};

const pc_codec *pc_codec_msgpack(void)
{
    return &s_msgpack;
}
#endif // PC_ENABLE_MSGPACK
