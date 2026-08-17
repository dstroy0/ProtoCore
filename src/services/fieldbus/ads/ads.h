// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ads.h
 * @brief Beckhoff ADS / AMS protocol codec (PROTOCORE_ENABLE_ADS) - zero-heap request builders +
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
 * the TCP connection (`protocore_client_*`) and the AMS route registration on the target router.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ADS_H
#define PROTOCORE_ADS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_ADS

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define ADS_TCP_PORT 48898     ///< AMS/TCP listening port (0xBF02)
#define ADS_AMSTCP_HDR_LEN 6   ///< reserved(2) + length(4)
#define ADS_AMS_HDR_LEN 32     ///< target/source net id + port, cmd, flags, cbData, error, invoke
#define ADS_HDR_LEN 38         ///< ADS_AMSTCP_HDR_LEN + ADS_AMS_HDR_LEN (payload starts here)
#define ADS_NET_ID_LEN 6       ///< an AMSNetId is six octets
#define ADS_DEVICE_NAME_LEN 16 ///< ReadDeviceInfo device-name field width

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

typedef enum PROTO_ENUM_PACKED
{
    ADS_TRANS_MODE_NO_TRANS = 0,
    ADS_TRANS_MODE_CLIENT_CYCLE = 1,
    ADS_TRANS_MODE_CLIENT_ON_CHANGE = 2,
    ADS_TRANS_MODE_SERVER_CYCLE = 3,     ///< server sends every CycleTime
    ADS_TRANS_MODE_SERVER_ON_CHANGE = 4, ///< server sends when the value changes
} AdsTransMode;

typedef struct
{
    uint8_t net_id[ADS_NET_ID_LEN];
    uint16_t port;
} AdsAmsAddr;

typedef struct
{
    AdsAmsAddr target;
    AdsAmsAddr source;
    uint32_t invoke_id;
} AdsRequest;

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

typedef struct
{
    uint32_t result; ///< ADS error code (0 = success)
    const uint8_t *data;
    uint32_t len;
} AdsReadResult;

typedef struct
{
    uint32_t result;
    uint16_t protocore_ads_state;
    uint16_t device_state;
} AdsReadStateResult;

typedef struct
{
    uint32_t result;
    uint8_t version_major;
    uint8_t version_minor;
    uint16_t version_build;
    char device_name[ADS_DEVICE_NAME_LEN + 1]; ///< NUL-terminated copy of the 16-octet field
} AdsDeviceInfo;

typedef void (*AdsNotificationSampleFn)(uint32_t notification_handle, const uint8_t *sample, uint32_t sample_len,
                                        uint64_t timestamp, void *user);

/** @brief What build_read_device_info takes: buf, cap, r. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const AdsRequest *r;
} AdsBuildReadDeviceInfoArgs;

/** @brief What build_read_state takes: buf, cap, r. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const AdsRequest *r;
} AdsBuildReadStateArgs;

/** @brief What build_read takes: buf, cap, r, index_group, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const AdsRequest *r;
    uint32_t index_group;
    uint32_t index_offset;
    uint32_t read_len;
} AdsBuildReadArgs;

/** @brief What build_write takes: buf, cap, r, index_group, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const AdsRequest *r;
    uint32_t index_group;
    uint32_t index_offset;
    const uint8_t *data;
    uint32_t len;
} AdsBuildWriteArgs;

/** @brief What build_read_write takes: buf, cap, r, index_group, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const AdsRequest *r;
    uint32_t index_group;
    uint32_t index_offset;
    uint32_t read_len;
    const uint8_t *write_data;
    uint32_t write_len;
} AdsBuildReadWriteArgs;

/** @brief What build_write_control takes: buf, cap, r, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const AdsRequest *r;
    uint16_t protocore_ads_state;
    uint16_t device_state;
    const uint8_t *data;
    uint32_t len;
} AdsBuildWriteControlArgs;

/** @brief What build_add_notification takes: buf, cap, r, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const AdsRequest *r;
    uint32_t index_group;
    uint32_t index_offset;
    uint32_t length;
    AdsTransMode mode;
    uint32_t max_delay;
    uint32_t cycle_time;
} AdsBuildAddNotificationArgs;

/** @brief What build_del_notification takes: buf, cap, r, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const AdsRequest *r;
    uint32_t notification_handle;
} AdsBuildDelNotificationArgs;

/** @brief What parse_ams_header takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    AdsAmsHeader *out;
} AdsParseAmsHeaderArgs;

/** @brief What parse_read takes: data, data_len, out. */
typedef struct
{
    const uint8_t *data;
    size_t data_len;
    AdsReadResult *out;
} AdsParseReadArgs;

/** @brief What parse_result takes: data, data_len, result. */
typedef struct
{
    const uint8_t *data;
    size_t data_len;
    uint32_t *result;
} AdsParseResultArgs;

/** @brief What parse_read_state takes: data, data_len, out. */
typedef struct
{
    const uint8_t *data;
    size_t data_len;
    AdsReadStateResult *out;
} AdsParseReadStateArgs;

/** @brief What parse_read_device_info takes: data, data_len, out. */
typedef struct
{
    const uint8_t *data;
    size_t data_len;
    AdsDeviceInfo *out;
} AdsParseReadDeviceInfoArgs;

/** @brief What parse_add_notification takes: data, data_len, result, ... */
typedef struct
{
    const uint8_t *data;
    size_t data_len;
    uint32_t *result;
    uint32_t *handle;
} AdsParseAddNotificationArgs;

/** @brief What parse_notification takes: data, data_len, on_sample, ... */
typedef struct
{
    const uint8_t *data;
    size_t data_len;
    AdsNotificationSampleFn on_sample;
    void *user;
} AdsParseNotificationArgs;

/**
 * @brief Beckhoff ADS / AMS protocol codec (PROTOCORE_ENABLE_ADS) - zero-heap request builders + response parsers for
 * TwinCAT PLCs over TCP 48898 (the PC-based-control protocol).
 *
 * A caller sets the members a call takes, invokes it through ::Ads with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Ads.build_read_device_info_args.buf = ...;
 *   Ads.build_read_device_info_args.cap = ...;
 *   Ads.build_read_device_info_args.r = ...;
 *   Ads.build_read_device_info(work);
 *   // Ads.n is what the call reports
 *
 * @var AdsNs::build_read_device_info_args  what build_read_device_info takes: buf, cap, r
 * @var AdsNs::build_read_state_args  what build_read_state takes: buf, cap, r
 * @var AdsNs::build_read_args  what build_read takes: buf, cap, r, index_group,
 * @var AdsNs::build_write_args  what build_write takes: buf, cap, r, index_group,
 * @var AdsNs::build_read_write_args  what build_read_write takes: buf, cap, r, index_group,
 * @var AdsNs::build_write_control_args  what build_write_control takes: buf, cap, r,
 * @var AdsNs::build_add_notification_args  what build_add_notification takes: buf, cap, r,
 * @var AdsNs::build_del_notification_args  what build_del_notification takes: buf, cap, r,
 * @var AdsNs::parse_ams_header_args  what parse_ams_header takes: buf, len, out
 * @var AdsNs::parse_read_args  what parse_read takes: data, data_len, out
 * @var AdsNs::parse_result_args  what parse_result takes: data, data_len, result
 * @var AdsNs::parse_read_state_args  what parse_read_state takes: data, data_len, out
 * @var AdsNs::parse_read_device_info_args  what parse_read_device_info takes: data, data_len, out
 * @var AdsNs::parse_add_notification_args  what parse_add_notification takes: data, data_len, result,
 * @var AdsNs::parse_notification_args  what parse_notification takes: data, data_len, on_sample,
 * @var AdsNs::ok  a call's true/false outcome
 * @var AdsNs::n  the count a call reports
 * @var AdsNs::build_read_device_info  build_read_device_info
 * @var AdsNs::build_read_state  build_read_state
 * @var AdsNs::build_read  build_read
 * @var AdsNs::build_write  build_write
 * @var AdsNs::build_read_write  build_read_write
 * @var AdsNs::build_write_control  build_write_control
 * @var AdsNs::build_add_notification  build_add_notification
 * @var AdsNs::build_del_notification  build_del_notification
 * @var AdsNs::parse_ams_header  parse_ams_header
 * @var AdsNs::parse_read  parse_read
 * @var AdsNs::parse_result  parse_result
 * @var AdsNs::parse_read_state  parse_read_state
 * @var AdsNs::parse_read_device_info  parse_read_device_info
 * @var AdsNs::parse_add_notification  parse_add_notification
 * @var AdsNs::parse_notification  parse_notification
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    AdsBuildReadDeviceInfoArgs build_read_device_info_args;
    AdsBuildReadStateArgs build_read_state_args;
    AdsBuildReadArgs build_read_args;
    AdsBuildWriteArgs build_write_args;
    AdsBuildReadWriteArgs build_read_write_args;
    AdsBuildWriteControlArgs build_write_control_args;
    AdsBuildAddNotificationArgs build_add_notification_args;
    AdsBuildDelNotificationArgs build_del_notification_args;
    AdsParseAmsHeaderArgs parse_ams_header_args;
    AdsParseReadArgs parse_read_args;
    AdsParseResultArgs parse_result_args;
    AdsParseReadStateArgs parse_read_state_args;
    AdsParseReadDeviceInfoArgs parse_read_device_info_args;
    AdsParseAddNotificationArgs parse_add_notification_args;
    AdsParseNotificationArgs parse_notification_args;

    proto_bool ok;
    size_t n;

    void (*const build_read_device_info)(uint8_t *restrict work);
    void (*const build_read_state)(uint8_t *restrict work);
    void (*const build_read)(uint8_t *restrict work);
    void (*const build_write)(uint8_t *restrict work);
    void (*const build_read_write)(uint8_t *restrict work);
    void (*const build_write_control)(uint8_t *restrict work);
    void (*const build_add_notification)(uint8_t *restrict work);
    void (*const build_del_notification)(uint8_t *restrict work);
    void (*const parse_ams_header)(uint8_t *restrict work);
    void (*const parse_read)(uint8_t *restrict work);
    void (*const parse_result)(uint8_t *restrict work);
    void (*const parse_read_state)(uint8_t *restrict work);
    void (*const parse_read_device_info)(uint8_t *restrict work);
    void (*const parse_add_notification)(uint8_t *restrict work);
    void (*const parse_notification)(uint8_t *restrict work);
} AdsNs;

/** @brief The one symbol this module exports. */
extern AdsNs Ads;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ADS

#endif // PROTOCORE_ADS_H
