// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file platform.h
 * @brief The platform contract: what the library asks of a target, in the library's own words.
 *
 * Nothing here names a vendor. The axis itself is resolved in vendor/vendor_detect.h, which then
 * pulls in exactly one vendor/<vendor>/ header; this file states the questions that header has to
 * answer and refuses the build, by name, for any it did not.
 *
 * Two kinds of thing live here:
 *   - the capability questions (PROTOCORE_HAS_*): does this part carry the thing at all. Each is
 *     `#ifndef`, so a build states what it has by defining it, and an unanswered one is an #error
 *     rather than a silent 0.
 *   - the seams (protocore_platform_*): declared under the capability that answers whether the part
 *     carries the thing, so reaching for an absent one is a compile error, not a link-time surprise.
 *
 * It is also the assembly point: the vendor axis, the board profile, the widths, the types built
 * from them, the capability floors and questions, and the seams - in the one order each depends on
 * the last. Nothing here includes a standard header; config/platform/types.h is the only file that
 * does.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PLATFORM_H
#define PROTOCORE_PLATFORM_H

// The vendor axis, then the one vendor header that answers the capability questions below.
#include "vendor/vendor_detect.h"
// Per-variant default sizing (chip / PSRAM / flash profiles). Reached before the widths so a board
// profile can state PROTOCORE_HW_WORD_BITS; a -D override still wins (every default is #ifndef).
#include "vendor/board_profiles/board_profile.h"
#include "config/platform/compiler_directives.h" // PROTOCORE_INLINE, settled before any body is parsed

// ---------------------------------------------------------------------------
// Platform widths
// ---------------------------------------------------------------------------
// The three numbers every primitive type and every lane mask is derived from
// (protocore_types.h, mmgr/swar.h). They are `#define`s rather than typedefs
// so they participate in preprocessor arithmetic, can be tested by `#if`, and can be overridden
// from build_opt.h or -D like every other knob. Each is checked below, so a bad value stops the
// build here, naming itself, instead of at the first expression that assumed it.

/**
 * @brief The target's natural register width, in bits.
 *
 * What a value is carried in while it is being worked on. Arithmetic narrower than the register is
 * not cheaper on any part in the target list - it costs the mask or sign-extend that keeps the
 * unused half correct - so the library states the register once and narrows only at a boundary.
 */
// The die states its register width in vendor/board_profiles/ (PROTOCORE_HW_WORD_BITS, floored at
// 32 for every part in the target list); this only names it. It is NOT read off the toolchain: the
// host toolchain is 64-bit, so inferring would give the host build 8-byte lane math, an 8-byte move
// ladder and 64-bit index arithmetic - a shape no target executes, measured on a machine that does
// not ship. A -D override still wins, which is how a 64-bit port or a width experiment is done.
#ifndef PROTO_WORD_BITS
#define PROTO_WORD_BITS PROTOCORE_HW_WORD_BITS
#endif

/**
 * @brief Bits in every offset, length and capacity the library declares (protocore_idx).
 *
 * Replaces `size_t`, whose width is inherited from the target's pointer and therefore differs
 * between a device build and the host test that proves it - the same source emitting different
 * index arithmetic in the two places it has to agree. 32 addresses far more than any pool reserved
 * here; a target whose every buffer is under 64 KB may set 16.
 */
#ifndef PROTO_INDEX_BITS
#define PROTO_INDEX_BITS 32
#endif

/**
 * @brief Bits in the lane carrier the byte-parallel scans and compares work in.
 *
 * Defaults to the register width, which is what makes the lane algebra worth doing: one word test
 * answers for PROTO_SWAR_BITS/8 bytes at once. Set it lower only to model a narrower machine - a
 * carrier WIDER than the register is synthesized from halves and is measurably slower than the
 * width it decomposes into. 8 is legal and degenerates to one lane per word, which is the honest
 * setting for a part with no wider register.
 */
#ifndef PROTO_SWAR_BITS
#define PROTO_SWAR_BITS PROTO_WORD_BITS
#endif


// The widths are settled above, so the types built from them come next. types.h is the one file in
// the library that includes a standard header.
#include "config/platform/types.h"

// What seams exist to call at all, then the capability questions the vendor had to answer.
#include "config/hardware_capabilities/hw_caps_en.h"
#include "config/hardware_capabilities/hw_caps_en_error.h"

// The library's own constants, and the seams each capability promises.
#include "config/platform/platform_defines.h"
#include "config/platform/platform_prototypes.h"
#include "config/hardware_capabilities/hw_caps_prototypes.h"

// Every width and ordering rule the above had to satisfy.
// The accelerator HALs the selected arm answers with, in the types settled above.
#include "vendor/vendor_hal.h"

#include "config/platform/platform_error.h"

#endif // PROTOCORE_PLATFORM_H
