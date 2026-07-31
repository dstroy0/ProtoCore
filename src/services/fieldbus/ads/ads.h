// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ads.h
 * @brief Beckhoff ADS / AMS protocol codec (PC_ENABLE_ADS) - zero-heap request builders +
 *        response parsers for TwinCAT PLCs over TCP 48898 (the PC-based-control protocol).
 *
 * ADS (Automation Device Specification) rides on AMS (Automation Message Specification). Every
 * multi-octet field is LITTLE-endian. A frame is an AMS/TCP header (6 octets) + an AMS header
 * (32 octets) + the command payload:
 * @code
 *   AMS/TCP header (6)
 *     00 00              reserved
 *     LL LL LL LL        length of everything after this field (AMS header + payload)
 *   AMS header (32)
 *     target net id (6)  e.g. 5.18....1.1  (AMSNetId, six octets in order)
 *     target port  (2)   e.g. 851 = the first TwinCAT 3 PLC runtime
 *     source net id (6)
 *     source port  (2)
 *     cmd id       (2)   1 ReadDeviceInfo 2 Read 3 Write 4 ReadState 5 WriteControl
 *                        6 AddNotification 7 DeleteNotification 8 Notification 9 ReadWrite
 *     state flags  (2)   0x0004 request, 0x0005 response (bit0 = response, bit2 = ADS command)
 *     data length  (4)   cbData - octets of payload that follow the AMS header
 *     error code   (4)   AMS error (0 = success)
 *     invoke id    (4)   caller-chosen, echoed in the response to correlate it
 *   payload (cbData)     command-specific (see the per-command builders/parsers below)
 * @endcode
 *
 * AMS header field order (target-before-source), command ids, and state flags verified against
 * the Beckhoff InfoSys AMS/ADS specification; payload layouts cross-checked with Beckhoff's own
 * open-source ADS library, `pyads`, and Apache PLC4X. Pure codec, host-tested - the caller owns
 * the TCP connection (`pc_client_*`) and the AMS route registration on the target router.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ADS_H
#define PROTOCORE_ADS_H

#include "protocore_config.h"

#if PC_ENABLE_ADS

#include <stddef.h>
#include <stdint.h>

#define ADS_TCP_PORT 48898     ///< AMS/TCP listening port (0xBF02)
#define ADS_AMSTCP_HDR_LEN 6   ///< reserved(2) + length(4)
#define ADS_AMS_HDR_LEN 32     ///< target/source net id + port, cmd, flags, cbData, error, invoke
#define ADS_HDR_LEN 38         ///< ADS_AMSTCP_HDR_LEN + ADS_AMS_HDR_LEN (payload starts here)
#define ADS_NET_ID_LEN 6       ///< an AMSNetId is six octets
#define ADS_DEVICE_NAME_LEN 16 ///< ReadDeviceInfo device-name field width

/// ADS command ids (AMS header octets 16-17). Cast to/from the wire only at the byte boundary.
enum class AdsCommand : uint16_t
{
    invalid = 0x0000,
    read_device_info = 0x0001,
    read = 0x0002,
    write = 0x0003,
    read_state = 0x0004,
    write_control = 0x0005,
    add_notification = 0x0006,
    del_notification = 0x0007,
    notification = 0x0008,
    read_write = 0x0009,
};

/// AMS header state-flag bits (octets 18-19). A TCP request is `pc_ads_command`; a response ORs in
/// `response`. Grouped as constants (not an enum) because they are a bitmask.
struct AdsStateFlags
{
    static constexpr uint16_t response = 0x0001;       ///< set on a response, clear on a request
    static constexpr uint16_t no_return = 0x0002;      ///< no response expected
    static constexpr uint16_t pc_ads_command = 0x0004; ///< ADS command (set for TCP)
    static constexpr uint16_t sys_command = 0x0008;    ///< system command
    static constexpr uint16_t high_prio = 0x0010;      ///< high priority
    static constexpr uint16_t timestamp = 0x0020;      ///< a timestamp is appended
    static constexpr uint16_t udp = 0x0040;            ///< carried over UDP
    static constexpr uint16_t init_command = 0x0080;
    static constexpr uint16_t broadcast = 0x8000;

    static constexpr uint16_t request = pc_ads_command;          ///< 0x0004
    static constexpr uint16_t reply = pc_ads_command | response; ///< 0x0005
};

/// ADS device state used by ReadState / WriteControl (a subset of ADSSTATE).
enum class AdsState : uint16_t
{
    invalid = 0,
    idle = 1,
    reset = 2,
    init = 3,
    start = 4,
    run = 5,
    stop = 6,
    save_config = 7,
    load_config = 8,
    power_failure = 9,
    power_good = 10,
    error = 11,
    shutdown = 12,
    suspend = 13,
    resume = 14,
    config = 15,
    reconfig = 16,
};

/// AddDeviceNotification transmission modes (ADSTRANS).
enum class AdsTransMode : uint32_t
{
    no_trans = 0,
    client_cycle = 1,
    client_on_change = 2,
    server_cycle = 3,     ///< server sends every CycleTime
    server_on_change = 4, ///< server sends when the value changes
};

/// Well-known ADS index groups for symbol access (dedup of the magic constants).
struct AdsIndexGroup
{
    static constexpr uint32_t sym_hnd_by_name = 0xF003;    ///< ReadWrite name -> handle
    static constexpr uint32_t sym_val_by_handle = 0xF005;  ///< Read/Write value by handle
    static constexpr uint32_t sym_release_handle = 0xF006; ///< Write to release a handle
    static constexpr uint32_t sym_info_by_name_ex = 0xF009;
    static constexpr uint32_t sym_upload = 0xF00B;
    static constexpr uint32_t sym_upload_info = 0xF00F;
    static constexpr uint32_t io_image_rw_ib = 0xF020; ///< %I input image, bit offset
    static constexpr uint32_t io_image_rw_ob = 0xF030; ///< %Q output image, bit offset
    static constexpr uint32_t plc_rw_m = 0x4020;       ///< %M flag memory, byte offset
    static constexpr uint32_t plc_rw_rb = 0x4030;      ///< retain memory
};

/// A 6-octet AMSNetId + a 2-octet AMS port (one endpoint of the AMS route).
struct AdsAmsAddr
{
    uint8_t net_id[ADS_NET_ID_LEN];
    uint16_t port;
};

/// Target/source addressing + invoke id carried on every request from one client.
struct AdsRequest
{
    AdsAmsAddr target;
    AdsAmsAddr source;
    uint32_t invoke_id;
};

/// A parsed AMS header; `data`/`data_len` point into the caller's buffer (no copy).
struct AdsAmsHeader
{
    AdsAmsAddr target;
    AdsAmsAddr source;
    AdsCommand cmd;
    uint16_t state_flags;
    uint32_t data_len;   ///< cbData
    uint32_t error_code; ///< AMS error (0 = success)
    uint32_t invoke_id;
    const uint8_t *data; ///< -> payload (into the caller's buffer)
};

/// Parsed Read / ReadWrite response payload (Result + Length + Data).
struct AdsReadResult
{
    uint32_t result; ///< ADS error code (0 = success)
    const uint8_t *data;
    uint32_t len;
};

/// Parsed ReadState response payload.
struct AdsReadStateResult
{
    uint32_t result;
    uint16_t pc_ads_state;
    uint16_t device_state;
};

/// Parsed ReadDeviceInfo response payload.
struct AdsDeviceInfo
{
    uint32_t result;
    uint8_t version_major;
    uint8_t version_minor;
    uint16_t version_build;
    char device_name[ADS_DEVICE_NAME_LEN + 1]; ///< NUL-terminated copy of the 16-octet field
};

// ---------------------------------------------------------------------------------------------
// Request builders. Each writes a complete on-wire frame (AMS/TCP + AMS header + payload) into
// `buf` and returns the total octet count, or 0 if `buf`/`r` is null or `cap` is too small.
// ---------------------------------------------------------------------------------------------

/// ReadDeviceInfo (cmd 1): no payload. Response = AdsDeviceInfo.
size_t pc_ads_build_read_device_info(uint8_t *buf, size_t cap, const AdsRequest *r);

/// ReadState (cmd 4): no payload. Response = AdsReadStateResult.
size_t pc_ads_build_read_state(uint8_t *buf, size_t cap, const AdsRequest *r);

/// Read (cmd 2): IndexGroup + IndexOffset + Length. Response = AdsReadResult.
size_t pc_ads_build_read(uint8_t *buf, size_t cap, const AdsRequest *r, uint32_t index_group, uint32_t index_offset,
                         uint32_t read_len);

/// Write (cmd 3): IndexGroup + IndexOffset + Length + Data. Response = a single result u32.
size_t pc_ads_build_write(uint8_t *buf, size_t cap, const AdsRequest *r, uint32_t index_group, uint32_t index_offset,
                          const uint8_t *data, uint32_t len);

/// ReadWrite (cmd 9): IndexGroup + IndexOffset + ReadLen + WriteLen + WriteData. The workhorse
/// for symbol-by-name (write the name to `sym_hnd_by_name`, read back the 4-octet handle).
/// Response = AdsReadResult.
size_t pc_ads_build_read_write(uint8_t *buf, size_t cap, const AdsRequest *r, uint32_t index_group,
                               uint32_t index_offset, uint32_t read_len, const uint8_t *write_data, uint32_t write_len);

/// WriteControl (cmd 5): AdsState + DeviceState + Length + Data. Response = a single result u32.
size_t pc_ads_build_write_control(uint8_t *buf, size_t cap, const AdsRequest *r, uint16_t pc_ads_state,
                                  uint16_t device_state, const uint8_t *data, uint32_t len);

/// AddDeviceNotification (cmd 6): subscribe to a symbol. Response = result u32 + handle u32
/// (parse with pc_ads_parse_add_notification). max_delay / cycle_time are in 100 ns units.
size_t pc_ads_build_add_notification(uint8_t *buf, size_t cap, const AdsRequest *r, uint32_t index_group,
                                     uint32_t index_offset, uint32_t length, AdsTransMode mode, uint32_t max_delay,
                                     uint32_t cycle_time);

/// DeleteDeviceNotification (cmd 7): NotificationHandle. Response = a single result u32.
size_t pc_ads_build_del_notification(uint8_t *buf, size_t cap, const AdsRequest *r, uint32_t notification_handle);

// ---------------------------------------------------------------------------------------------
// Response parsers. `pc_ads_parse_ams_header` validates the framing and exposes the payload; the
// per-command parsers then decode that payload. Each returns false on a short/garbled buffer.
// ---------------------------------------------------------------------------------------------

/// Validate the AMS/TCP + AMS framing and fill `out` (its `data` points into `buf`).
bool pc_ads_parse_ams_header(const uint8_t *buf, size_t len, AdsAmsHeader *out);

/// Read / ReadWrite response payload: Result(4) + Length(4) + Data(Length).
bool pc_ads_parse_read(const uint8_t *data, size_t data_len, AdsReadResult *out);

/// Write / WriteControl / DeleteNotification response payload: a single Result(4).
bool pc_ads_parse_result(const uint8_t *data, size_t data_len, uint32_t *result);

/// ReadState response payload: Result(4) + AdsState(2) + DeviceState(2).
bool pc_ads_parse_read_state(const uint8_t *data, size_t data_len, AdsReadStateResult *out);

/// ReadDeviceInfo response payload: Result(4) + Major(1) + Minor(1) + Build(2) + Name(16).
bool pc_ads_parse_read_device_info(const uint8_t *data, size_t data_len, AdsDeviceInfo *out);

/// AddDeviceNotification response payload: Result(4) + NotificationHandle(4).
bool pc_ads_parse_add_notification(const uint8_t *data, size_t data_len, uint32_t *result, uint32_t *handle);

/// Callback invoked once per sample while walking a DeviceNotification (cmd 8) payload.
/// `timestamp` is the raw Windows FILETIME (100 ns ticks since 1601-01-01 UTC).
using AdsNotificationSampleFn = void (*)(uint32_t notification_handle, const uint8_t *sample, uint32_t sample_len,
                                         uint64_t timestamp, void *user);

/// Walk a DeviceNotification payload (Length + Stamps, each stamp = Timestamp + Samples + the
/// per-sample handle/size/data), calling `on_sample` for every sample. Returns false if the
/// buffer is truncated or internally inconsistent.
bool pc_ads_parse_notification(const uint8_t *data, size_t data_len, AdsNotificationSampleFn on_sample, void *user);

#endif // PC_ENABLE_ADS

#endif // PROTOCORE_ADS_H
