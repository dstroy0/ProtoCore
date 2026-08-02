// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ld2410.h
 * @brief HLK-LD2410 24 GHz mmWave presence / motion radar codec (PC_ENABLE_LD2410).
 *
 * The LD2410 streams a framed serial report at 256000 baud: header `F4 F3 F2 F1`, a
 * little-endian intra-frame length, the payload, and footer `F8 F7 F6 F5`. The payload carries
 * the target state (none / moving / stationary / both), the moving and stationary target
 * distance (cm) and energy (0-100), the overall detection distance, and - in "engineering
 * mode" - the per-gate energy of all nine range gates. Configuration is a second frame kind
 * (header `FD FC FB FA`, footer `04 03 02 01`) carrying a 2-byte command word.
 *
 * This codec is pure and host-tested: ::pc_ld2410_parse_report decodes one report frame, and
 * ::Ld2410Stream reassembles frames byte-by-byte from a UART with resync on noise (no heap,
 * fixed buffer). The command encoders build the config frames. On an ESP32 the binding pumps a
 * `HardwareSerial` and keeps the latest report; only that read/write touches hardware.
 *
 * A cheap solder-and-bench-test breakout: wire it to a UART, wave a hand, watch presence.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LD2410_H
#define PROTOCORE_LD2410_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

#if PC_ENABLE_LD2410

/** @brief Range gates the LD2410 reports energy for in engineering mode (gate 0..8). */
#define LD2410_MAX_GATES 9

/** @brief Largest assembled frame: header(4) + len(2) + payload(<=60) + footer(4), rounded up. */
#define LD2410_FRAME_MAX 72

/** @brief Target presence state (report payload byte 2). */
#define LD2410_STATE_NONE 0x00   ///< no target
#define LD2410_STATE_MOVING 0x01 ///< moving target only
#define LD2410_STATE_STATIC 0x02 ///< stationary target only
#define LD2410_STATE_BOTH 0x03   ///< both a moving and a stationary target

/** @brief A decoded LD2410 target report. Engineering fields are 0 unless @ref engineering. */
typedef struct
{
    uint8_t engineering;   ///< 1 if this was an engineering-mode frame (per-gate energies valid)
    uint8_t state;         ///< one of LD2410_STATE_*
    uint16_t moving_cm;    ///< moving target distance (cm)
    uint8_t moving_energy; ///< moving target energy (0-100)
    uint16_t static_cm;    ///< stationary target distance (cm)
    uint8_t static_energy; ///< stationary target energy (0-100)
    uint16_t detect_cm;    ///< overall detection distance (cm)
    // Engineering mode only:
    uint8_t max_moving_gate;                      ///< highest configured moving gate
    uint8_t max_static_gate;                      ///< highest configured stationary gate
    uint8_t moving_gate_energy[LD2410_MAX_GATES]; ///< per-gate moving energy (0-100)
    uint8_t static_gate_energy[LD2410_MAX_GATES]; ///< per-gate stationary energy (0-100)
    uint8_t light;                                ///< photosensor level (0-255)
    uint8_t out_pin;                              ///< OUT pin level (0/1)
} Ld2410Report;

/**
 * @brief Decode one whole LD2410 report frame (header `F4 F3 F2 F1` .. footer `F8 F7 F6 F5`).
 * Pure - no I/O. Validates the header/footer, the intra-frame length, the data-type byte
 * (0x02 basic / 0x01 engineering), the `0xAA` head marker and the `0x55` tail.
 * @return true on a valid frame; false on any mismatch or a short buffer.
 */
proto_bool pc_ld2410_parse_report(const uint8_t *frame, size_t len, Ld2410Report *out);

/** @brief Byte-by-byte report-frame reassembler (fixed buffer, resyncs on noise). */
typedef struct
{
    uint8_t buf[LD2410_FRAME_MAX]; ///< frame under construction
    uint16_t pos;                  ///< bytes collected so far
    uint16_t total;                ///< expected full-frame length (known after the length field)
    uint8_t hdr_match;             ///< header bytes matched while syncing (phase = pos<4)
    uint8_t phase;                 ///< 0 sync, 1 length, 2 body
} Ld2410Stream;

/** @brief Reset a stream to the syncing state. */
void pc_ld2410_stream_reset(Ld2410Stream *s);

/**
 * @brief Feed one received byte. When it completes a valid report frame, fills @p out and
 * returns true; otherwise returns false (still syncing / mid-frame / bad frame - it resyncs).
 */
proto_bool pc_ld2410_stream_push(Ld2410Stream *s, uint8_t byte, Ld2410Report *out);

/** @brief true if @p r shows any target (moving or stationary). */
proto_bool pc_ld2410_present(const Ld2410Report *r);

/** @brief Best available target distance (cm): the moving distance if moving, else stationary. */
uint16_t pc_ld2410_distance_cm(const Ld2410Report *r);

// --- Config-command encoders (build a full `FD FC FB FA` .. `04 03 02 01` frame) -------------
// Each returns the frame length written, or 0 if @p cap is too small. Config commands must be
// bracketed by enable/end; engineering + restart take effect inside that bracket.

/** @brief "Enable configuration" (word 0x00FF, value 0x0001). */
size_t pc_ld2410_cmd_config_enable(uint8_t *buf, size_t cap);
/** @brief "End configuration" (word 0x00FE). */
size_t pc_ld2410_cmd_config_end(uint8_t *buf, size_t cap);
/** @brief Enable (0x0062) or disable (0x0063) engineering mode. */
size_t pc_ld2410_cmd_engineering(uint8_t *buf, size_t cap, proto_bool on);
/** @brief Restart the module (word 0x00A3). */
size_t pc_ld2410_cmd_restart(uint8_t *buf, size_t cap);

// --- LD2410B-only config commands ------------------------------------------------------------
// The LD2410B is HiLink's BLE-equipped build of the same radar and speaks this identical
// `FD FC FB FA` protocol, so everything above applies to it unchanged. The three commands below
// exist only on the B: they configure the Bluetooth radio over the *wired* UART. The BLE control
// channel itself is out of scope here - this is the serial side only. Each is still bracketed by
// enable/end like any other config command.

/** @brief LD2410B: turn the Bluetooth radio on (value 0x0001) or off (0x0000). Word 0x00A4. */
size_t pc_ld2410_cmd_bluetooth(uint8_t *buf, size_t cap, proto_bool on);

/** @brief LD2410B: query the module's Bluetooth MAC address (word 0x00A5, value 0x0001). */
size_t pc_ld2410_cmd_get_mac(uint8_t *buf, size_t cap);

/**
 * @brief LD2410B: set the 6-octet Bluetooth control password (word 0x00A9). Takes effect after a
 *        restart and survives power loss. @p password is exactly 6 octets, sent in natural order
 *        (the factory default is the ASCII "HiLink" -> 48 69 4C 69 6E 6B); it is not NUL-terminated
 *        and is not padded, so pass 6 octets.
 *
 * The companion "obtain Bluetooth access" command (0x00A8) is deliberately absent: the protocol
 * document states it answers only over Bluetooth and not the serial port, so it belongs to the BLE
 * control channel, which this wired driver does not cover.
 */
size_t pc_ld2410_cmd_set_bt_password(uint8_t *buf, size_t cap, const char password[6]);

// --- Command-ACK decoding --------------------------------------------------------------------
// The module answers every config command with a frame in the same `FD FC FB FA` envelope, whose
// command word is the request's word with 0x0100 set (0x00A5 -> 0x01A5) followed by a 2-octet
// status. Needed to read a query's result (the MAC) and to tell an accepted command from a
// rejected one; the report stream (`F4 F3 F2 F1`) is a separate frame kind and is unaffected.

/** @brief A decoded command-ACK frame. @ref payload points into the caller's frame (not copied). */
typedef struct
{
    uint16_t command;       ///< ACK command word: the request word | 0x0100 (e.g. 0x01A5 for get-MAC).
    uint16_t status;        ///< 0 = success, 1 = failure.
    const uint8_t *payload; ///< command-specific data after the status word (nullptr if none).
    size_t payload_len;     ///< octets at @ref payload.
} Ld2410Ack;

/**
 * @brief Decode one command-ACK frame (header, intra-frame length, footer and length agreement all
 *        checked). @return false if @p frame is not a well-formed ACK.
 */
proto_bool pc_ld2410_parse_ack(const uint8_t *frame, size_t len, Ld2410Ack *out);

/** @brief True if @p ack reports success (@ref Ld2410Ack::status == 0). */
proto_bool pc_ld2410_ack_ok(const Ld2410Ack *ack);

/**
 * @brief Extract the 6-octet MAC from a get-MAC ACK (word 0x01A5) into @p mac, in wire order.
 * @return false unless @p ack is a successful get-MAC ACK carrying at least 6 payload octets.
 */
proto_bool pc_ld2410_ack_mac(const Ld2410Ack *ack, uint8_t mac[6]);

// --- ESP32 binding (Serial pump; no-ops on a host build) -------------------------------------

/** @brief Open UART2 at PC_LD2410_BAUD on @p rx_pin / @p tx_pin. @return true on ESP32. */
proto_bool pc_ld2410_begin(int rx_pin, int tx_pin);

/** @brief Pump the UART through the stream. @return true if a fresh report was decoded. */
proto_bool pc_ld2410_poll();

/** @brief The most recently decoded report, or nullptr before the first one arrives. */
const Ld2410Report *pc_ld2410_last();

/** @brief Enable/disable engineering mode (brackets the command with enable/end). */
proto_bool pc_ld2410_set_engineering(proto_bool on);

/** @brief Restart the module (brackets the command with enable/end). */
proto_bool pc_ld2410_restart();

#endif // PC_ENABLE_LD2410

#endif // PROTOCORE_LD2410_H
