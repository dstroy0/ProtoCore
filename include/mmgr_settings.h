// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mmgr_settings.h
 * @brief The tunables of MMgr in ProtoCore. MMgr reads this header from the include path, before its first
 *        tunable.
 *
 * Each build path of ProtoCore has include/ on its include path. MMgr and ProtoCore thus read the same
 * values. The Arduino IDE gives a library no compile definition, and there this header is the only way to
 * give them.
 *
 * Each receive buffer of the idemIP stack is one loculus of an MMgr ring. A ring has 32 loculi, the bits
 * of a 32-bit word. The state of a ring holds the record of each loculus. The ring words thus rise with them.
 * A compile definition of the same name replaces a value here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MMGR_SETTINGS_H
#define PROTOCORE_MMGR_SETTINGS_H

/**
 * @brief The alignment of each MMgr pool: one cache line of 32 bytes, IDEMIP_CACHE_LINE_BYTES of idemIP.
 *
 * @note idemip_config.h asserts that this covers a cache line, because a DMA pool starts on a line. A
 *       unit that reads MMgr before idemIP thus gets the same value that idemIP gets.
 */
#ifndef MMGR_ALIGN_BYTES
#define MMGR_ALIGN_BYTES 32u
#endif

#ifndef MMGR_RING_LOCULI
#define MMGR_RING_LOCULI 32u
#endif

#ifndef MMGR_RING_WORDS
#define MMGR_RING_WORDS 106u
#endif

#endif // PROTOCORE_MMGR_SETTINGS_H
