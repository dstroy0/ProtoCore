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

#define ADS_TCP_PORT 48898     ///< AMS/TCP listening port (0xBF02)
#define ADS_AMSTCP_HDR_LEN 6   ///< reserved(2) + length(4)
#define ADS_AMS_HDR_LEN 32     ///< target/source net id + port, cmd, flags, cbData, error, invoke
#define ADS_HDR_LEN 38         ///< ADS_AMSTCP_HDR_LEN + ADS_AMS_HDR_LEN (payload starts here)
#define ADS_NET_ID_LEN 6       ///< an AMSNetId is six octets
#define ADS_DEVICE_NAME_LEN 16 ///< ReadDeviceInfo device-name field width

/// ADS command ids (AMS header octets 16-17). Cast to/from the wire only at the byte boundary.
typedef enum PROTO_ENUM_PACKED
{
    ADS_COMMAND_INVALID = 0x0000,
    ADS_COMMAND_READ_DEVICE_INFO = 0x0001,
    ADS_COMMAND_READ = 0x0002,
    ADS_COMMAND_WRITE = 0x0003,
    ADS_COMMAND_READ_STATE = 0x0004,
    ADS_COMMAND_WRITE_CONTROL = 0x0005,
    ADS_COMMAND_ADD_NOTIFICATION = 0x0006,
    ADS_COMMAND_DEL_NOTIFICATION = 0x0007,
    ADS_COMMAND_NOTIFICATION = 0x0008,
    ADS_COMMAND_READ_WRITE = 0x0009,
} AdsCommand;

/// AMS header state-flag bits (octets 18-19). A TCP request is ADS_STATE_ADS_COMMAND; a response
/// ORs in ADS_STATE_RESPONSE.
#define ADS_STATE_RESPONSE 0x0001    ///< set on a response, clear on a request
#define ADS_STATE_NO_RETURN 0x0002   ///< no response expected
#define ADS_STATE_ADS_COMMAND 0x0004 ///< ADS command (set for TCP)
#define ADS_STATE_SYS_COMMAND 0x0008 ///< system command
#define ADS_STATE_HIGH_PRIO 0x0010   ///< high priority
#define ADS_STATE_TIMESTAMP 0x0020   ///< a timestamp is appended
#define ADS_STATE_UDP 0x0040         ///< carried over UDP
#define ADS_STATE_INIT_COMMAND 0x0080
#define ADS_STATE_BROADCAST 0x8000
#define ADS_STATE_REQUEST ADS_STATE_ADS_COMMAND                      ///< 0x0004
#define ADS_STATE_REPLY (ADS_STATE_ADS_COMMAND | ADS_STATE_RESPONSE) ///< 0x0005

/// ADS device state used by ReadState / WriteControl (a subset of ADSSTATE).
typedef enum PROTO_ENUM_PACKED
{
    ADS_STATE_INVALID = 0,
    ADS_STATE_IDLE = 1,
    ADS_STATE_RESET = 2,
    ADS_STATE_INIT = 3,
    ADS_STATE_START = 4,
    ADS_STATE_RUN = 5,
    ADS_STATE_STOP = 6,
    ADS_STATE_SAVE_CONFIG = 7,
    ADS_STATE_LOAD_CONFIG = 8,
    ADS_STATE_POWER_FAILURE = 9,
    ADS_STATE_POWER_GOOD = 10,
    ADS_STATE_ERROR = 11,
    ADS_STATE_SHUTDOWN = 12,
    ADS_STATE_SUSPEND = 13,
    ADS_STATE_RESUME = 14,
    ADS_STATE_CONFIG = 15,
    ADS_STATE_RECONFIG = 16,
} AdsState;

/// AddDeviceNotification transmission modes (ADSTRANS).
typedef enum PROTO_ENUM_PACKED
{
    ADS_TRANS_MODE_NO_TRANS = 0,
    ADS_TRANS_MODE_CLIENT_CYCLE = 1,
    ADS_TRANS_MODE_CLIENT_ON_CHANGE = 2,
    ADS_TRANS_MODE_SERVER_CYCLE = 3,     ///< server sends every CycleTime
    ADS_TRANS_MODE_SERVER_ON_CHANGE = 4, ///< server sends when the value changes
} AdsTransMode;

/// Well-known ADS index groups for symbol access (dedup of the magic constants).
#define ADS_IGRP_SYM_HND_BY_NAME 0xF003    ///< ReadWrite name -> handle
#define ADS_IGRP_SYM_VAL_BY_HANDLE 0xF005  ///< Read/Write value by handle
#define ADS_IGRP_SYM_RELEASE_HANDLE 0xF006 ///< Write to release a handle
#define ADS_IGRP_SYM_INFO_BY_NAME_EX 0xF009
#define ADS_IGRP_SYM_UPLOAD 0xF00B
#define ADS_IGRP_SYM_UPLOAD_INFO 0xF00F
#define ADS_IGRP_IO_IMAGE_RW_IB 0xF020 ///< %I input image, bit offset
#define ADS_IGRP_IO_IMAGE_RW_OB 0xF030 ///< %Q output image, bit offset
#define ADS_IGRP_PLC_RW_M 0x4020       ///< %M flag memory, byte offset
#define ADS_IGRP_PLC_RW_RB 0x4030      ///< retain memory

/// A 6-octet AMSNetId + a 2-octet AMS port (one endpoint of the AMS route).
typedef struct
{
    uint8_t net_id[ADS_NET_ID_LEN];
    uint16_t port;
} AdsAmsAddr;

/// Target/source addressing + invoke id carried on every request from one client.
typedef struct
{
    AdsAmsAddr target;
    AdsAmsAddr source;
    uint32_t invoke_id;
} AdsRequest;

/// A parsed AMS header; `data`/`data_len` point into the caller's buffer (no copy).
typedef struct
{
    AdsAmsAddr target;
    AdsAmsAddr source;
    AdsCommand cmd;
    uint16_t state_flags;
    uint32_t data_len;   ///< cbData
    uint32_t error_code; ///< AMS error (0 = success)
    uint32_t invoke_id;
    const uint8_t *data; ///< -> payload (into the caller's buffer)
} AdsAmsHeader;

/// Parsed Read / ReadWrite response payload (Result + Length + Data).
typedef struct
{
    uint32_t result; ///< ADS error code (0 = success)
    const uint8_t *data;
    uint32_t len;
} AdsReadResult;

/// Parsed ReadState response payload.
typedef struct
{
    uint32_t result;
    uint16_t pc_ads_state;
    uint16_t device_state;
} AdsReadStateResult;

/// Parsed ReadDeviceInfo response payload.
typedef struct
{
    uint32_t result;
    uint8_t version_major;
    uint8_t version_minor;
    uint16_t version_build;
    char device_name[ADS_DEVICE_NAME_LEN + 1]; ///< NUL-terminated copy of the 16-octet field
} AdsDeviceInfo;

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
proto_bool pc_ads_parse_ams_header(const uint8_t *buf, size_t len, AdsAmsHeader *out);

/// Read / ReadWrite response payload: Result(4) + Length(4) + Data(Length).
proto_bool pc_ads_parse_read(const uint8_t *data, size_t data_len, AdsReadResult *out);

/// Write / WriteControl / DeleteNotification response payload: a single Result(4).
proto_bool pc_ads_parse_result(const uint8_t *data, size_t data_len, uint32_t *result);

/// ReadState response payload: Result(4) + AdsState(2) + DeviceState(2).
proto_bool pc_ads_parse_read_state(const uint8_t *data, size_t data_len, AdsReadStateResult *out);

/// ReadDeviceInfo response payload: Result(4) + Major(1) + Minor(1) + Build(2) + Name(16).
proto_bool pc_ads_parse_read_device_info(const uint8_t *data, size_t data_len, AdsDeviceInfo *out);

/// AddDeviceNotification response payload: Result(4) + NotificationHandle(4).
proto_bool pc_ads_parse_add_notification(const uint8_t *data, size_t data_len, uint32_t *result, uint32_t *handle);

/// Callback invoked once per sample while walking a DeviceNotification (cmd 8) payload.
/// `timestamp` is the raw Windows FILETIME (100 ns ticks since 1601-01-01 UTC).
typedef void (*AdsNotificationSampleFn)(uint32_t notification_handle, const uint8_t *sample, uint32_t sample_len,
                                        uint64_t timestamp, void *user);

/// Walk a DeviceNotification payload (Length + Stamps, each stamp = Timestamp + Samples + the
/// per-sample handle/size/data), calling `on_sample` for every sample. Returns false if the
/// buffer is truncated or internally inconsistent.
proto_bool pc_ads_parse_notification(const uint8_t *data, size_t data_len, AdsNotificationSampleFn on_sample,
                                     void *user);

#endif // PC_ENABLE_ADS

#endif // PROTOCORE_ADS_H
