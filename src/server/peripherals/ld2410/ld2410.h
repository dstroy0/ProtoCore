// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ld2410.h
 * @brief HLK-LD2410 24 GHz mmWave presence / motion radar codec (PROTOCORE_ENABLE_LD2410).
 *
 * The LD2410 streams a framed serial report at 256000 baud: header `F4 F3 F2 F1`, a
 * little-endian intra-frame length, the payload, and footer `F8 F7 F6 F5`. The payload carries
 * the target state (none / moving / stationary / both), the moving and stationary target
 * distance (cm) and energy (0-100), the overall detection distance, and - in "engineering
 * mode" - the per-gate energy of all nine range gates. Configuration is a second frame kind
 * (header `FD FC FB FA`, footer `04 03 02 01`) carrying a 2-byte command word.
 *
 * This codec is pure and host-tested: ::protocore_ld2410_parse_report decodes one report frame, and
 * ::Ld2410Stream reassembles frames byte-by-byte from a UART with resync on noise (no heap,
 * fixed buffer). The command encoders build the config frames. Where a bus seam exists the binding
 * pumps a UART through uart.h and keeps the latest report; only that read/write reaches the seam.
 *
 * A cheap solder-and-bench-test breakout: wire it to a UART, wave a hand, watch presence.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LD2410_H
#define PROTOCORE_LD2410_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_LD2410

PROTOCORE_BEGIN_DECLS

// PROTOCORE_LD2410_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define LD2410_MAX_GATES 9

#define LD2410_FRAME_MAX 72

#define LD2410_STATE_NONE 0x00 ///< no target

#define LD2410_STATE_MOVING 0x01 ///< moving target only

#define LD2410_STATE_STATIC 0x02 ///< stationary target only

#define LD2410_STATE_BOTH 0x03 ///< both a moving and a stationary target

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
/** @brief Byte-by-byte report-frame reassembler (fixed buffer, resyncs on noise). */
typedef struct
{
    uint8_t buf[LD2410_FRAME_MAX]; ///< frame under construction
    uint16_t pos;                  ///< bytes collected so far
    uint16_t total;                ///< expected full-frame length (known after the length field)
    uint8_t hdr_match;             ///< header bytes matched while syncing (phase = pos<4)
    uint8_t phase;                 ///< 0 sync, 1 length, 2 body
} Ld2410Stream;
/** @brief A decoded command-ACK frame. @ref payload points into the caller's frame (not copied). */
typedef struct
{
    uint16_t command;       ///< ACK command word: the request word | 0x0100 (e.g. 0x01A5 for get-MAC).
    uint16_t status;        ///< 0 = success, 1 = failure.
    const uint8_t *payload; ///< command-specific data after the status word (nullptr if none).
    size_t payload_len;     ///< octets at @ref payload.
} Ld2410Ack;
/** @brief What parse_report takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    Ld2410Report *out;
} Ld2410ParseReportArgs;
/** @brief What stream_reset takes: s. */
typedef struct
{
    Ld2410Stream *s;
} Ld2410StreamResetArgs;
/** @brief What stream_push takes: s, byte, out. */
typedef struct
{
    Ld2410Stream *s;
    uint8_t byte;
    Ld2410Report *out;
} Ld2410StreamPushArgs;
/** @brief What present takes: r. */
typedef struct
{
    const Ld2410Report *r;
} Ld2410PresentArgs;
/** @brief What distance_cm takes: r. */
typedef struct
{
    const Ld2410Report *r;
} Ld2410DistanceCmArgs;
/** @brief What cmd_config_enable takes: buf, cap. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
} Ld2410CmdConfigEnableArgs;
/** @brief What cmd_config_end takes: buf, cap. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
} Ld2410CmdConfigEndArgs;
/** @brief What cmd_engineering takes: buf, cap, on. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    proto_bool on;
} Ld2410CmdEngineeringArgs;
/** @brief What cmd_restart takes: buf, cap. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
} Ld2410CmdRestartArgs;
/** @brief What cmd_bluetooth takes: buf, cap, on. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    proto_bool on;
} Ld2410CmdBluetoothArgs;
/** @brief What cmd_get_mac takes: buf, cap. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
} Ld2410CmdGetMacArgs;
/** @brief What cmd_set_bt_password takes: buf, cap, password. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const char *password; ///< 6 bytes.
} Ld2410CmdSetBtPasswordArgs;
/** @brief What parse_ack takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    Ld2410Ack *out;
} Ld2410ParseAckArgs;
/** @brief What ack_ok takes: ack. */
typedef struct
{
    const Ld2410Ack *ack;
} Ld2410AckOkArgs;
/** @brief What ack_mac takes: ack, mac. */
typedef struct
{
    const Ld2410Ack *ack;
    uint8_t *mac; ///< 6 bytes.
} Ld2410AckMacArgs;
/** @brief What begin takes: rx_pin, tx_pin. */
typedef struct
{
    int rx_pin;
    int tx_pin;
} Ld2410BeginArgs;
/** @brief What set_engineering takes: on. */
typedef struct
{
    proto_bool on;
} Ld2410SetEngineeringArgs;
/**
 * @brief HLK-LD2410 24 GHz mmWave presence / motion radar codec (PROTOCORE_ENABLE_LD2410). The LD2410 streams a framed
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::Ld2410 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Ld2410.parse_report_args.frame = ...;
 *   Ld2410.parse_report_args.len = ...;
 *   Ld2410.parse_report_args.out = ...;
 *   Ld2410.parse_report(work);
 *   // Ld2410.ok is what the call reports
 *
 * @var Ld2410Ns::parse_report_args  what parse_report takes: frame, len, out
 * @var Ld2410Ns::stream_reset_args  what stream_reset takes: s
 * @var Ld2410Ns::stream_push_args  what stream_push takes: s, byte, out
 * @var Ld2410Ns::present_args  what present takes: r
 * @var Ld2410Ns::distance_cm_args  what distance_cm takes: r
 * @var Ld2410Ns::cmd_config_enable_args  what cmd_config_enable takes: buf, cap
 * @var Ld2410Ns::cmd_config_end_args  what cmd_config_end takes: buf, cap
 * @var Ld2410Ns::cmd_engineering_args  what cmd_engineering takes: buf, cap, on
 * @var Ld2410Ns::cmd_restart_args  what cmd_restart takes: buf, cap
 * @var Ld2410Ns::cmd_bluetooth_args  what cmd_bluetooth takes: buf, cap, on
 * @var Ld2410Ns::cmd_get_mac_args  what cmd_get_mac takes: buf, cap
 * @var Ld2410Ns::cmd_set_bt_password_args  what cmd_set_bt_password takes: buf, cap, password
 * @var Ld2410Ns::parse_ack_args  what parse_ack takes: frame, len, out
 * @var Ld2410Ns::ack_ok_args  what ack_ok takes: ack
 * @var Ld2410Ns::ack_mac_args  what ack_mac takes: ack, mac
 * @var Ld2410Ns::begin_args  what begin takes: rx_pin, tx_pin
 * @var Ld2410Ns::set_engineering_args  what set_engineering takes: on
 * @var Ld2410Ns::ok  true on a valid frame; false on any mismatch or a short buffer
 * @var Ld2410Ns::cm  what a call reports
 * @var Ld2410Ns::n  the count a call reports
 * @var Ld2410Ns::report  what a call reports
 * @var Ld2410Ns::parse_report  decode one whole LD2410 report frame (header `F4 F3 F2 F1` .. ...
 * @var Ld2410Ns::stream_reset  reset a stream to the syncing state
 * @var Ld2410Ns::stream_push  feed one received byte. When it completes a valid report frame, ...
 * @var Ld2410Ns::present  true if r shows any target (moving or stationary)
 * @var Ld2410Ns::distance_cm  best available target distance (cm): the moving distance if moving, ...
 * @var Ld2410Ns::cmd_config_enable  "Enable configuration" (word 0x00FF, value 0x0001)
 * @var Ld2410Ns::cmd_config_end  "End configuration" (word 0x00FE)
 * @var Ld2410Ns::cmd_engineering  enable (0x0062) or disable (0x0063) engineering mode
 * @var Ld2410Ns::cmd_restart  restart the module (word 0x00A3)
 * @var Ld2410Ns::cmd_bluetooth  LD2410B: turn the Bluetooth radio on (value 0x0001) or off ...
 * @var Ld2410Ns::cmd_get_mac  LD2410B: query the module's Bluetooth MAC address (word 0x00A5, ...
 * @var Ld2410Ns::cmd_set_bt_password  LD2410B: set the 6-octet Bluetooth control password (word 0x00A9). ...
 * @var Ld2410Ns::parse_ack  decode one command-ACK frame (header, intra-frame length, footer ...
 * @var Ld2410Ns::ack_ok  true if ack reports success (Ld2410Ack::status == 0)
 * @var Ld2410Ns::ack_mac  extract the 6-octet MAC from a get-MAC ACK (word 0x01A5) into mac, ...
 * @var Ld2410Ns::begin  open PROTOCORE_LD2410_UART at PROTOCORE_LD2410_BAUD on rx_pin / ...
 * @var Ld2410Ns::poll  pump the UART through the stream. true if a fresh report was decoded
 * @var Ld2410Ns::last  the most recently decoded report, or NULL before the first one ...
 * @var Ld2410Ns::set_engineering  enable/disable engineering mode (brackets the command with ...
 * @var Ld2410Ns::restart  restart the module (brackets the command with enable/end)
 *
 * @c work is PROTOCORE_LD2410_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Ld2410ParseReportArgs parse_report_args;
    Ld2410StreamResetArgs stream_reset_args;
    Ld2410StreamPushArgs stream_push_args;
    Ld2410PresentArgs present_args;
    Ld2410DistanceCmArgs distance_cm_args;
    Ld2410CmdConfigEnableArgs cmd_config_enable_args;
    Ld2410CmdConfigEndArgs cmd_config_end_args;
    Ld2410CmdEngineeringArgs cmd_engineering_args;
    Ld2410CmdRestartArgs cmd_restart_args;
    Ld2410CmdBluetoothArgs cmd_bluetooth_args;
    Ld2410CmdGetMacArgs cmd_get_mac_args;
    Ld2410CmdSetBtPasswordArgs cmd_set_bt_password_args;
    Ld2410ParseAckArgs parse_ack_args;
    Ld2410AckOkArgs ack_ok_args;
    Ld2410AckMacArgs ack_mac_args;
    Ld2410BeginArgs begin_args;
    Ld2410SetEngineeringArgs set_engineering_args;
    proto_bool ok;
    uint16_t cm;
    size_t n;
    const Ld2410Report *report;
} Ld2410Vars;

/** @brief The operands and the outcome. */
extern Ld2410Vars Ld2410V;

/** @brief The entries. */
typedef struct
{
    void (*const parse_report)(uint8_t *restrict work);
    void (*const stream_reset)(uint8_t *restrict work);
    void (*const stream_push)(uint8_t *restrict work);
    void (*const present)(uint8_t *restrict work);
    void (*const distance_cm)(uint8_t *restrict work);
    void (*const cmd_config_enable)(uint8_t *restrict work);
    void (*const cmd_config_end)(uint8_t *restrict work);
    void (*const cmd_engineering)(uint8_t *restrict work);
    void (*const cmd_restart)(uint8_t *restrict work);
    void (*const cmd_bluetooth)(uint8_t *restrict work);
    void (*const cmd_get_mac)(uint8_t *restrict work);
    void (*const cmd_set_bt_password)(uint8_t *restrict work);
    void (*const parse_ack)(uint8_t *restrict work);
    void (*const ack_ok)(uint8_t *restrict work);
    void (*const ack_mac)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const poll)(uint8_t *restrict work);
    void (*const last)(uint8_t *restrict work);
    void (*const set_engineering)(uint8_t *restrict work);
    void (*const restart)(uint8_t *restrict work);
} Ld2410Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Ld2410V or a region of the borrow at a fixed offset.
void protocore_ld2410_parse_report(uint8_t *restrict work);
void protocore_ld2410_stream_reset(uint8_t *restrict work);
void protocore_ld2410_stream_push(uint8_t *restrict work);
void protocore_ld2410_present(uint8_t *restrict work);
void protocore_ld2410_distance_cm(uint8_t *restrict work);
void protocore_ld2410_cmd_config_enable(uint8_t *restrict work);
void protocore_ld2410_cmd_config_end(uint8_t *restrict work);
void protocore_ld2410_cmd_engineering(uint8_t *restrict work);
void protocore_ld2410_cmd_restart(uint8_t *restrict work);
void protocore_ld2410_cmd_bluetooth(uint8_t *restrict work);
void protocore_ld2410_cmd_get_mac(uint8_t *restrict work);
void protocore_ld2410_cmd_set_bt_password(uint8_t *restrict work);
void protocore_ld2410_parse_ack(uint8_t *restrict work);
void protocore_ld2410_ack_ok(uint8_t *restrict work);
void protocore_ld2410_ack_mac(uint8_t *restrict work);
void protocore_ld2410_begin(uint8_t *restrict work);
void protocore_ld2410_poll(uint8_t *restrict work);
void protocore_ld2410_last(uint8_t *restrict work);
void protocore_ld2410_set_engineering(uint8_t *restrict work);
void protocore_ld2410_restart(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Ld2410.parse_report(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Ld2410Ns Ld2410 __attribute__((unused)) = {
    .parse_report = protocore_ld2410_parse_report,
    .stream_reset = protocore_ld2410_stream_reset,
    .stream_push = protocore_ld2410_stream_push,
    .present = protocore_ld2410_present,
    .distance_cm = protocore_ld2410_distance_cm,
    .cmd_config_enable = protocore_ld2410_cmd_config_enable,
    .cmd_config_end = protocore_ld2410_cmd_config_end,
    .cmd_engineering = protocore_ld2410_cmd_engineering,
    .cmd_restart = protocore_ld2410_cmd_restart,
    .cmd_bluetooth = protocore_ld2410_cmd_bluetooth,
    .cmd_get_mac = protocore_ld2410_cmd_get_mac,
    .cmd_set_bt_password = protocore_ld2410_cmd_set_bt_password,
    .parse_ack = protocore_ld2410_parse_ack,
    .ack_ok = protocore_ld2410_ack_ok,
    .ack_mac = protocore_ld2410_ack_mac,
    .begin = protocore_ld2410_begin,
    .poll = protocore_ld2410_poll,
    .last = protocore_ld2410_last,
    .set_engineering = protocore_ld2410_set_engineering,
    .restart = protocore_ld2410_restart,
};

/**
 * @brief The PROTOCORE_LD2410_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_ld2410_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_LD2410

#endif // PROTOCORE_LD2410_H
