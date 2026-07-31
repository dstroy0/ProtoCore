// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file focas.h
 * @brief FANUC FOCAS Ethernet protocol codec (PC_ENABLE_FOCAS) - zero-heap request builders +
 *        response parsers for FANUC CNCs over TCP 8193 (the machine-tool data protocol).
 *
 * FOCAS ("FANUC Open CNC API Specification") is normally a proprietary PC library (fwlib32); this
 * is a pure codec for its Ethernet wire protocol. Every multi-octet field is BIG-endian. A frame
 * is a 10-octet envelope + a payload:
 * @code
 *   envelope (10)
 *     A0 A0 A0 A0        frame magic
 *     version    (2)     = 1
 *     frame type (2)     0x0101 open req / 0x0102 open resp
 *                        0x2101 command (VAR) req / 0x2102 command resp
 *                        0x0201 close req / 0x0202 close resp
 *     length     (2)     octets of payload that follow
 *   payload (length)     type-specific (see below)
 * @endcode
 *
 * The session is: open (payload = FRAME_DST 0x0002) -> one or more commands -> close. A command
 * request payload is a 6-octet selector + five signed 32-bit arguments + optional extra data:
 * @code
 *   c1 c2 c3           three u16: the FOCAS function selector (e.g. 1/1/0x18 = SysInfo)
 *   v1 v2 v3 v4 v5     five i32: the function's integer arguments
 *   extra...           function-specific trailing bytes (none for the reads below)
 * @endcode
 * A command response payload echoes the 6-octet selector, then a 6-octet status (a signed-short
 * FOCAS return code in its first two octets, 0 = EW_OK), then a u16 data length, then the data.
 *
 * Frame layout, selector encoding, the open/close handshake, the SysInfo (`ODBSYS`) and alarm
 * (`>L`) response layouts, and the 8-octet value encoding (`data / base^exp`) were reverse-
 * engineered by and cross-checked against diohpix/pyfanuc. Function selectors are the verbatim
 * set documented there. Pure codec, host-tested - the caller owns the TCP connection
 * (`pc_client_*`) and drives the open -> command -> close sequence.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FOCAS_H
#define PROTOCORE_FOCAS_H

#include "protocore_config.h"

#if PC_ENABLE_FOCAS

#include <stddef.h>
#include <stdint.h>

#define FOCAS_TCP_PORT 8193    ///< FOCAS Ethernet listening port
#define FOCAS_FRAME_HDR_LEN 10 ///< magic(4) + version(2) + type(2) + length(2)
#define FOCAS_PROTO_VERSION 1  ///< envelope version field
#define FOCAS_FRAME_DST 0x0002 ///< open-request payload (FRAME_DST)
#define FOCAS_CMD_SEL_LEN 6    ///< the c1/c2/c3 selector (three u16)
#define FOCAS_CMD_ARGS_LEN 20  ///< v1..v5 (five i32)
#define FOCAS_REQ_BODY_LEN 26  ///< FOCAS_CMD_SEL_LEN + FOCAS_CMD_ARGS_LEN
#define FOCAS_RESP_HDR_LEN 14  ///< echoed selector(6) + status(6) + data length(2)
#define FOCAS_VALUE_LEN 8      ///< one FOCAS 8-octet numeric value
#define FOCAS_SYSINFO_LEN 18   ///< ODBSYS: addinfo+maxaxis+cnctype+mttype+series+version+axes

/// Frame types (envelope octets 6-7). Cast to/from the wire only at the byte boundary.
enum class FocasFrameType : uint16_t
{
    invalid = 0x0000,
    open_req = 0x0101,
    open_resp = 0x0102,
    close_req = 0x0201,
    close_resp = 0x0202,
    command_req = 0x2101, ///< FTYPE_VAR_REQU
    command_resp = 0x2102 ///< FTYPE_VAR_RESP
};

/// A FOCAS function selector: three big-endian u16 (c1, c2, c3).
struct FocasCmd
{
    uint16_t c1;
    uint16_t c2;
    uint16_t c3;
};

/// The documented FOCAS function selectors (verbatim from the pyfanuc protocol notes). Grouped as
/// constants (not an enum) because a selector is a struct, not a single scalar.
struct FocasCommand
{
    static constexpr FocasCmd read_cnc_param = {1, 1, 0x0e};  ///< cnc_rdparam
    static constexpr FocasCmd read_macro = {1, 1, 0x15};      ///< cnc_rdmacro
    static constexpr FocasCmd set_macro = {1, 1, 0x16};       ///< cnc_wrmacro
    static constexpr FocasCmd sys_info = {1, 1, 0x18};        ///< cnc_sysinfo (ODBSYS)
    static constexpr FocasCmd read_alarm = {1, 1, 0x1a};      ///< cnc_alarm (u32 status word)
    static constexpr FocasCmd read_prg_num = {1, 1, 0x1c};    ///< cnc_rdprgnum (main/running)
    static constexpr FocasCmd read_seq_num = {1, 1, 0x1d};    ///< cnc_rdseqnum
    static constexpr FocasCmd read_alarm_info = {1, 1, 0x23}; ///< cnc_rdalminfo
    static constexpr FocasCmd read_feedrate = {1, 1, 0x24};   ///< cnc_actf (actual feed)
    static constexpr FocasCmd read_spindle = {1, 1, 0x25};    ///< cnc_acts (actual spindle speed)
    static constexpr FocasCmd read_position = {1, 1, 0x26};   ///< cnc_rdposition / axis read
    static constexpr FocasCmd read_diag = {1, 1, 0x30};       ///< cnc_diagnoss
    static constexpr FocasCmd read_spindle2 = {1, 1, 0x40};   ///< cnc_acts2 (speed + load)
    static constexpr FocasCmd read_datetime = {1, 1, 0x45};   ///< cnc_rdtimer (v1=0 date, 1 time)
    static constexpr FocasCmd read_servo_load = {1, 1, 0x56}; ///< servo load, MAX_AXIS
    static constexpr FocasCmd read_axis_names = {1, 1, 0x89}; ///< controlled-axis names
    static constexpr FocasCmd read_spindle_names = {1, 1, 0x8a};
    static constexpr FocasCmd read_cnc_param3 = {1, 1, 0x8d}; ///< cnc_rdparam3
    static constexpr FocasCmd read_macro_dbl = {1, 1, 0xa7};  ///< cnc_rdmacror (double)
    static constexpr FocasCmd read_pmc = {2, 1, 0x8001};      ///< pmc_rdpmcrng
};

/// Position/axis read kinds (SysInfo 0x26 `v1`); the axis argument is `v2` (0 = all axes).
struct FocasPosKind
{
    static constexpr int32_t machine = 1;  ///< machine (reference) coordinate
    static constexpr int32_t absolute = 4; ///< absolute (program) coordinate
    static constexpr int32_t relative = 6; ///< relative coordinate
    static constexpr int32_t distance = 7; ///< distance to go
    static constexpr int32_t skip = 8;     ///< skip position
};

/// A parsed frame envelope; `payload`/`payload_len` point into the caller's buffer (no copy).
struct FocasFrame
{
    FocasFrameType type;
    uint16_t version;
    const uint8_t *payload;
    uint16_t payload_len;
};

/// A parsed command response; `data`/`data_len` point into the caller's buffer (no copy).
struct FocasResponse
{
    uint16_t c1; ///< echoed selector
    uint16_t c2;
    uint16_t c3;
    int16_t status; ///< FOCAS return code (0 = EW_OK; negative = error)
    const uint8_t *data;
    uint16_t data_len;
};

/// Parsed SysInfo (ODBSYS). The char fields are NUL-terminated copies of fixed-width ASCII fields.
struct FocasSysInfo
{
    uint16_t add_info;
    uint16_t max_axis;
    char cnc_type[3]; ///< e.g. "30", "16", "0i"
    char mt_type[3];  ///< " M" milling / " T" turning
    char series[5];   ///< software series
    char version[5];  ///< software version
    char axes[3];     ///< controlled-axis count as ASCII
};

/// One decoded FOCAS 8-octet numeric value. The scaled value is `data / base^exp`; `valid` is
/// false for the 0xFFFF sentinel or an unrecognized base (only 2 and 10 are decimal-scaled).
struct FocasValue
{
    int32_t data;
    uint8_t base; ///< 2 or 10
    uint8_t exp;  ///< decimal places
    bool valid;
};

// ---------------------------------------------------------------------------------------------
// Request builders. Each writes a complete on-wire frame into `buf` and returns the total octet
// count, or 0 if `buf` is null or `cap` is too small.
// ---------------------------------------------------------------------------------------------

/// Open the session (payload = FRAME_DST). Send first; expect a 0x0102 response frame.
size_t pc_focas_build_open(uint8_t *buf, size_t cap);

/// Close the session (empty payload). Send last; expect a 0x0202 response frame.
size_t pc_focas_build_close(uint8_t *buf, size_t cap);

/// Generic command: selector + five i32 arguments + optional trailing `extra` bytes.
size_t pc_focas_build_request(uint8_t *buf, size_t cap, FocasCmd cmd, int32_t v1, int32_t v2, int32_t v3, int32_t v4,
                              int32_t v5, const uint8_t *extra, size_t extra_len);

/// SysInfo (1/1/0x18): no arguments. Response = FocasSysInfo.
size_t pc_focas_build_sysinfo(uint8_t *buf, size_t cap);

/// Alarm status (1/1/0x1a): no arguments. Response = a big-endian u32 alarm bitmask.
size_t pc_focas_build_read_alarm(uint8_t *buf, size_t cap);

/// Read CNC parameter(s) (1/1/0x0e): parameter range [first, last], axis (0 = not axis-specific).
size_t pc_focas_build_read_param(uint8_t *buf, size_t cap, int32_t first, int32_t last, int32_t axis);

/// Read macro variables (1/1/0x15): variable range [first, last]. Response values are 8-octet.
size_t pc_focas_build_read_macro(uint8_t *buf, size_t cap, int32_t first, int32_t last);

/// Read position/axis data (1/1/0x26): `kind` (FocasPosKind), `axis` (0 = all). 8-octet values.
size_t pc_focas_build_read_position(uint8_t *buf, size_t cap, int32_t kind, int32_t axis);

/// Read actual feedrate (1/1/0x24): no arguments. Response = one 8-octet value.
size_t pc_focas_build_read_feedrate(uint8_t *buf, size_t cap);

/// Read actual spindle speed (1/1/0x25): no arguments. Response = one 8-octet value.
size_t pc_focas_build_read_spindle(uint8_t *buf, size_t cap);

// ---------------------------------------------------------------------------------------------
// Response parsers. Each returns false on a short/garbled buffer.
// ---------------------------------------------------------------------------------------------

/// Validate the 10-octet envelope (magic + version) and expose the payload (into `buf`).
bool pc_focas_parse_frame(const uint8_t *buf, size_t len, FocasFrame *out);

/// Decode a command-response payload (echoed selector + status + length + data) into `out`.
bool pc_focas_parse_response(const uint8_t *payload, size_t payload_len, FocasResponse *out);

/// Convenience: validate a whole command-response frame (type 0x2102) straight into `out`.
bool pc_focas_parse_command_frame(const uint8_t *buf, size_t len, FocasResponse *out);

/// SysInfo response data: ODBSYS (addinfo + maxaxis + cnctype + mttype + series + version + axes).
bool pc_focas_parse_sysinfo(const uint8_t *data, size_t data_len, FocasSysInfo *out);

/// Alarm response data: a single big-endian u32 alarm bitmask.
bool pc_focas_parse_alarm(const uint8_t *data, size_t data_len, uint32_t *alarm_status);

/// Decode one FOCAS 8-octet value at `chunk`. Returns true only for a usable value (`out->valid`
/// is set the same way); false if fewer than 8 octets are available.
bool pc_focas_decode8(const uint8_t *chunk, size_t len, FocasValue *out);

/// The scaled value `data / base^exp` as a float (0 for an invalid value).
float pc_focas_value_f(const FocasValue *v);

#endif // PC_ENABLE_FOCAS

#endif // PROTOCORE_FOCAS_H
