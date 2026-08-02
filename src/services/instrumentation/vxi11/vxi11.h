// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file vxi11.h
 * @brief VXI-11 (TCP/IP Instrument Protocol) codec over ONC RPC / XDR (PC_ENABLE_VXI11) - a
 *        zero-heap codec for the legacy LXI instrument transport that predates HiSLIP.
 *
 * VXI-11 rides on ONC RPC (Sun RPC, RFC 5531) with XDR (RFC 4506) over TCP. A session:
 *   portmap GETPORT(0x0607AF, 1, TCP) -> the DEVICE_CORE port ; connect there ;
 *   create_link("inst0") -> a link id ; device_write("*IDN?\n") ; device_read() -> the reply ;
 *   destroy_link().
 *
 * This codec provides the reusable ONC-RPC framing (the TCP record-marking header + the CALL /
 * accepted-REPLY message headers with AUTH_NONE) over XDR (big-endian, 4-byte aligned,
 * length-prefixed opaque/string - no sub-word types on the wire), plus the VXI-11 DEVICE_CORE
 * procedure builders + response parsers and the portmapper GETPORT call. Pairs with @c
 * PC_ENABLE_SCPI (the payload). Pure codec, host-tested; the TCP connection is the application's.
 *
 * References: VXI-11 (VXIbus Consortium, 1995); RFC 5531 (ONC RPC), RFC 4506 (XDR), RFC 1833 (portmap).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_VXI11_H
#define PROTOCORE_VXI11_H

#include "protocore_config.h"

#if PC_ENABLE_VXI11

// ── ONC RPC / portmapper / VXI-11 program constants ─────────────────────────────────────────────
#define PC_VXI11_CORE_PROG 0x0607AFu ///< DEVICE_CORE RPC program number
#define PC_VXI11_CORE_VERS 1         ///< DEVICE_CORE version
#define PC_RPC_PMAP_PROG 100000u     ///< portmapper / rpcbind v2 program
#define PC_RPC_PMAP_VERS 2
#define PC_RPC_PMAP_PORT 111    ///< the well-known portmapper TCP port
#define PC_RPC_PMAP_GETPORT 3   ///< PMAPPROC_GETPORT
#define PC_RPC_PROTO_TCP 6      ///< IPPROTO_TCP (for the GETPORT mapping)
#define PC_RPC_AUTH_NONE 0      ///< the AUTH_NONE auth flavor
#define PC_RPC_MSG_ACCEPTED 0   ///< reply_stat: the call was accepted
#define PC_RPC_ACCEPT_SUCCESS 0 ///< accept_stat: the procedure ran

/** @brief VXI-11 DEVICE_CORE procedure numbers (procs 21 and 24 are intentionally unused). */
typedef enum PROTO_ENUM_PACKED
{
    VXI11_PROC_CREATE_LINK = 10,
    VXI11_PROC_DEVICE_WRITE = 11,
    VXI11_PROC_DEVICE_READ = 12,
    VXI11_PROC_DEVICE_READSTB = 13,
    VXI11_PROC_DEVICE_TRIGGER = 14,
    VXI11_PROC_DEVICE_CLEAR = 15,
    VXI11_PROC_DEVICE_REMOTE = 16,
    VXI11_PROC_DEVICE_LOCAL = 17,
    VXI11_PROC_DEVICE_LOCK = 18,
    VXI11_PROC_DEVICE_UNLOCK = 19,
    VXI11_PROC_DEVICE_ENABLE_SRQ = 20,
    VXI11_PROC_DEVICE_DOCMD = 22,
    VXI11_PROC_DESTROY_LINK = 23,
    VXI11_PROC_CREATE_INTR_CHAN = 25,
    VXI11_PROC_DESTROY_INTR_CHAN = 26,
} Vxi11Proc;

// Device_Flags bits:
#define PC_VXI11_FLAG_WAITLOCK 0x01   ///< block up to lock_timeout on lock contention
#define PC_VXI11_FLAG_END 0x08        ///< (write) assert END/EOI with the last byte
#define PC_VXI11_FLAG_TERMCHRSET 0x80 ///< (read) termChar is an active read terminator
// device_read `reason` return bits (may combine):
#define PC_VXI11_REASON_REQCNT 0x01 ///< requestSize bytes were transferred
#define PC_VXI11_REASON_CHR 0x02    ///< the termChar was seen
#define PC_VXI11_REASON_END 0x04    ///< an END/EOI indicator was received
// Device_ErrorCode (common values):
#define PC_VXI11_ERR_NONE 0
#define PC_VXI11_ERR_SYNTAX 1
#define PC_VXI11_ERR_NOT_ACCESSIBLE 3
#define PC_VXI11_ERR_INVALID_LINK 4
#define PC_VXI11_ERR_PARAMETER 5
#define PC_VXI11_ERR_NO_LOCK 12
#define PC_VXI11_ERR_IO_TIMEOUT 15
#define PC_VXI11_ERR_IO_ERROR 17
#define PC_VXI11_ERR_ABORT 23

/** @brief A short human-readable string for a Device_ErrorCode (static; never null). */
const char *pc_vxi11_error_str(int32_t error);

// ── reusable ONC RPC framing ────────────────────────────────────────────────────────────────────

/**
 * @brief Write the 4-byte TCP record-marking header for a single-fragment message: the high bit is
 *        the last-fragment flag (always set here), the low 31 bits are @p payload_len.
 * @return 4, or 0 if @p cap < 4 or @p payload_len exceeds 31 bits.
 */
size_t pc_rpc_record_mark(uint8_t *buf, size_t cap, uint32_t payload_len);

/**
 * @brief Parse a 4-byte record-marking header.
 * @param last     receives the last-fragment flag.
 * @param frag_len receives the fragment payload byte length (excludes the 4 RM bytes).
 * @return true if @p len >= 4; false otherwise.
 */
proto_bool pc_rpc_parse_record_mark(const uint8_t *buf, size_t len, proto_bool *last, uint32_t *frag_len);

/**
 * @brief Parse an accepted ONC-RPC reply header (the bytes AFTER the record mark): xid, REPLY,
 *        MSG_ACCEPTED, verf, accept_stat.
 * @param xid         receives the transaction id (echoes the call).
 * @param accept_stat receives the accept_stat (0 = SUCCESS; the caller checks before reading results).
 * @param result_off  receives the offset (into @p rpc) where the procedure results begin.
 * @return true on a well-formed accepted reply; false if truncated, not a REPLY, or MSG_DENIED.
 */
proto_bool pc_rpc_parse_reply(const uint8_t *rpc, size_t len, uint32_t *xid, uint32_t *accept_stat, size_t *result_off);

// ── portmapper ──────────────────────────────────────────────────────────────────────────────────

/**
 * @brief Build a PMAPPROC_GETPORT call (with record mark): maps (prog, vers, proto) to a TCP port.
 * @return total bytes written (incl. the 4-byte record mark), or 0 on overflow.
 */
size_t pc_vxi11_build_getport(uint8_t *buf, size_t cap, uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proto);

/**
 * @brief Parse a GETPORT reply (bytes after the record mark). @p port is 0 if not registered.
 * @return true on a well-formed successful reply; false otherwise.
 */
proto_bool pc_vxi11_parse_getport_resp(const uint8_t *rpc, size_t len, uint32_t *port);

// ── VXI-11 DEVICE_CORE ──────────────────────────────────────────────────────────────────────────

/**
 * @brief Build a create_link call: open a link to @p device (e.g. "inst0"). @p client_id is
 *        caller-chosen; @p lock_device requests an exclusive lock.
 * @return total bytes written (incl. record mark), or 0 on overflow / bad input.
 */
size_t pc_vxi11_build_create_link(uint8_t *buf, size_t cap, uint32_t xid, int32_t client_id, proto_bool lock_device,
                                  uint32_t lock_timeout, const char *device);

/** @brief A decoded Create_LinkResp. */
typedef struct
{
    int32_t error;          ///< Device_ErrorCode (0 = no error)
    int32_t lid;            ///< the link id, for subsequent calls
    uint32_t abort_port;    ///< the DEVICE_ASYNC abort-channel port
    uint32_t max_recv_size; ///< the largest data block the device accepts in one device_write
} Vxi11CreateLinkResp;
proto_bool pc_vxi11_parse_create_link_resp(const uint8_t *rpc, size_t len, Vxi11CreateLinkResp *out);

/**
 * @brief Build a device_write call: write @p data (e.g. a SCPI command) to link @p lid. Set
 *        @ref PC_VXI11_FLAG_END in @p flags to assert END with the last byte.
 * @return total bytes written (incl. record mark), or 0 on overflow / bad input.
 */
size_t pc_vxi11_build_device_write(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid, uint32_t io_timeout,
                                   uint32_t lock_timeout, uint32_t flags, const uint8_t *data, size_t data_len);

/** @brief A decoded Device_WriteResp. */
typedef struct
{
    int32_t error;
    uint32_t size; ///< number of bytes written
} Vxi11WriteResp;
proto_bool pc_vxi11_parse_write_resp(const uint8_t *rpc, size_t len, Vxi11WriteResp *out);

/**
 * @brief Build a device_read call: read up to @p request_size bytes from link @p lid. Set
 *        @ref PC_VXI11_FLAG_TERMCHRSET in @p flags to stop at @p term_char.
 * @return total bytes written (incl. record mark), or 0 on overflow.
 */
size_t pc_vxi11_build_device_read(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid, uint32_t request_size,
                                  uint32_t io_timeout, uint32_t lock_timeout, uint32_t flags, uint8_t term_char);

/** @brief A decoded Device_ReadResp. @ref data points INTO @p rpc. */
typedef struct
{
    int32_t error;
    int32_t reason; ///< OR of PC_VXI11_REASON_* (why the read ended)
    const uint8_t *data;
    size_t data_len;
} Vxi11ReadResp;
proto_bool pc_vxi11_parse_read_resp(const uint8_t *rpc, size_t len, Vxi11ReadResp *out);

/**
 * @brief Build a device_readstb call: read the IEEE 488.2 status byte from link @p lid.
 * @return total bytes written (incl. record mark), or 0 on overflow.
 */
size_t pc_vxi11_build_device_readstb(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid, uint32_t flags,
                                     uint32_t lock_timeout, uint32_t io_timeout);

/** @brief A decoded Device_ReadStbResp. */
typedef struct
{
    int32_t error;
    uint8_t stb; ///< the status byte
} Vxi11ReadStbResp;
proto_bool pc_vxi11_parse_readstb_resp(const uint8_t *rpc, size_t len, Vxi11ReadStbResp *out);

/**
 * @brief Build a device_clear call: clear link @p lid's device (the protocol-level Selected Device Clear -
 *        it empties the instrument's input / output buffers). Carries the Device_GenericParms (lid + flags +
 *        the lock / io timeouts), as device_readstb does. @return total bytes written, or 0 on overflow.
 */
size_t pc_vxi11_build_device_clear(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid, uint32_t flags,
                                   uint32_t lock_timeout, uint32_t io_timeout);

/**
 * @brief Build a device_trigger call: assert a trigger on link @p lid (the protocol-level Group Execute
 *        Trigger, i.e. IEEE 488.2 `*TRG`). Same Device_GenericParms as device_clear. @return bytes, or 0.
 */
size_t pc_vxi11_build_device_trigger(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid, uint32_t flags,
                                     uint32_t lock_timeout, uint32_t io_timeout);

/**
 * @brief Build a destroy_link call: close link @p lid.
 * @return total bytes written (incl. record mark), or 0 on overflow.
 */
size_t pc_vxi11_build_destroy_link(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid);

/**
 * @brief Parse a bare Device_Error result (destroy_link / device_trigger / device_clear / ...).
 * @param error receives the Device_ErrorCode.
 * @return true on a well-formed successful reply; false otherwise.
 */
proto_bool pc_vxi11_parse_error_resp(const uint8_t *rpc, size_t len, int32_t *error);

#endif // PC_ENABLE_VXI11

#endif // PROTOCORE_VXI11_H
