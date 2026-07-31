// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file exc_decoder.h
 * @brief ESP32 panic / exception decoder for a live diagnostics panel (PC_ENABLE_EXC_DECODER).
 *
 * When an ESP32 panics it prints a Guru Meditation dump: a cause ("LoadProhibited"), a per-core register
 * dump (PC, EXCVADDR, ...), and a backtrace of `PC:SP` frame pairs. To resolve those PCs to file:line an
 * addr2line-style panel needs the firmware ELF, which lives off-device - so the on-device job is to
 * *extract and present* the raw decode: the cause, the faulting PC + data address, and the ordered
 * backtrace PC list, served as JSON for a live "/exception" panel (the browser or a build server then
 * resolves symbols). This is that extractor.
 *
 * It parses the panic text an app captures (from the console, a saved crash line, or a text rendering of
 * the core-dump partition) into a structured ExcInfo, and serializes it. Pure, zero heap, no stdlib
 * (hand-rolled hex/decimal parsing), host-testable against a captured panic string.
 */

#ifndef PROTOCORE_EXC_DECODER_H
#define PROTOCORE_EXC_DECODER_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_EXC_DECODER

#ifndef PC_EXC_MAX_FRAMES
#define PC_EXC_MAX_FRAMES 32 ///< backtrace frames retained (Xtensa panics rarely exceed this).
#endif

/** @brief One backtrace frame: a program counter and its stack pointer. */
struct ExcFrame
{
    uint32_t pc;
    uint32_t sp;
};

/** @brief A decoded panic. Fields not found in the input are left at their zeroed / -1 defaults. */
struct ExcInfo
{
    int core;                           ///< panicking core number, or -1 if not present.
    char cause[32];                     ///< exception cause text (e.g. "LoadProhibited"), "" if absent.
    uint32_t pc;                        ///< faulting PC (register-dump PC, else first backtrace frame).
    uint32_t excvaddr;                  ///< faulting data address (EXCVADDR), 0 if absent.
    bool has_excvaddr;                  ///< true if an EXCVADDR field was present.
    ExcFrame frames[PC_EXC_MAX_FRAMES]; ///< backtrace, outermost-first as printed.
    size_t frame_count;
};

/**
 * @brief Parse an ESP32 panic dump into @p out.
 *
 * Recognizes the Guru Meditation cause, the core number, the register-dump PC + EXCVADDR, and the
 * `Backtrace: pc:sp pc:sp ...` frame list. Tolerant of missing fields and of a trailing "|<-CORRUPTED".
 * @return true if at least one of {cause, pc, a backtrace frame} was found.
 */
bool pc_exc_parse(const char *text, ExcInfo *out);

/**
 * @brief Serialize a decoded panic as
 * `{"core":N,"cause":"..","pc":"0x..","excvaddr":"0x..","backtrace":["0x..",...]}`.
 * `core` is omitted when -1; `excvaddr` is omitted when absent.
 * @return length written (excl NUL), or 0 on overflow / bad args.
 */
size_t pc_exc_json(const ExcInfo *info, char *out, size_t cap);

#if defined(ARDUINO)
// --- Core-dump partition (ESP32) ---------------------------------------------------------
//
// A panic that reboots the device takes its console output with it. ESP-IDF also writes a core
// dump to a flash partition, which survives the reboot - so the next boot can report what crashed
// and hand the raw image off somewhere durable before it is overwritten by the next crash.
//
// Requires a `coredump` partition in the partition table (the default Arduino tables have one).

namespace fs
{
class FS;
}

/** @brief Where the stored core dump lives, and how big it is. */
struct ExcCoreDump
{
    uint32_t addr; ///< absolute flash address of the image
    size_t size;   ///< image size in bytes
};

/**
 * @brief Is a core dump stored, and is its checksum intact?
 * @param out optional; filled with the image address/size when one is present.
 * @return true if a valid dump is waiting to be read.
 */
bool pc_exc_coredump_present(ExcCoreDump *out);

/**
 * @brief Fill an ExcInfo from the stored dump's summary, so the existing `/exception` panel can
 *        render a crash recovered after reboot rather than only a live console capture.
 *
 * Architecture differs, and the result says so honestly:
 *  - **Xtensa** (ESP32 / S2 / S3): a real backtrace is recorded on-device, so `frames` is populated
 *    (pc only; sp is not part of the summary) along with the exception cause and faulting address.
 *  - **RISC-V** (C3 / C6 / H2 / P4): the summary carries a stack dump, not a backtrace - resolving
 *    it needs DWARF off-device. `frame_count` is therefore 0 and only the faulting PC and the
 *    machine trap cause / value are filled. Offload the image (pc_exc_coredump_save) and resolve it
 *    with `esp-coredump` or GDB against the firmware ELF.
 *
 * @return true if a summary was read.
 */
bool pc_exc_coredump_summary(ExcInfo *out);

/**
 * @brief Read @p len raw bytes of the stored image starting at @p offset within it.
 *
 * The transport-agnostic seam: the image is pulled in whatever chunks the consumer wants, so a
 * dump can go to a filesystem, up an FTP control/data pair, into an HTTP POST body, or anywhere
 * else without this owner knowing about any of them. @p offset is relative to the start of the
 * image (byte 0 = the size word), not to the flash partition.
 *
 * @return true if the whole range was read; false if no valid dump is stored or the range runs
 *         past its end (a short read is never reported as success).
 */
bool pc_exc_coredump_read(size_t offset, void *buf, size_t len);

/**
 * @brief Copy the raw core-dump image out of flash into a file (SD, LittleFS, ...).
 *
 * Streams in PC_EXC_COREDUMP_CHUNK-sized pieces, so it costs no heap and a large dump does not
 * need to fit RAM.
 *
 * The written file is the **raw core-dump image** in ESP-IDF's flash format, not a bare ELF: a
 * 24-byte header (the first word is the total image size) followed by an `ET_CORE` ELF at offset 24
 * and a trailing checksum. Feed it to the host tool as raw - `esp-coredump info_corefile -c
 * <file> -t raw <firmware.elf>` - or skip the first 24 bytes to get a plain ELF. It is stored
 * verbatim so nothing is lost in translation.
 *
 * @return true if the whole image was written.
 */
bool pc_exc_coredump_save(fs::FS &file_sys, const char *path);

/**
 * @brief Erase the stored dump so the next boot does not re-offload the same crash.
 *        Call only after a successful save.
 */
bool pc_exc_coredump_erase(void);
#endif // ARDUINO

#endif // PC_ENABLE_EXC_DECODER
#endif // PROTOCORE_EXC_DECODER_H
