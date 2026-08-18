// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file session.h
 * @brief Layer 5 (Session) - where a connection is opened, closed and controlled.
 *
 * A connection's life is decided here: the transport signals that one arrived, ended or faulted,
 * and this layer turns that into an open, a close, or a dispatch to whichever protocol owns the
 * slot. It sits above the server core and uses it rather than being part of it - the worker pool
 * turns the crank (server/core/worker.h) and the protocol registry says who receives the event
 * (server/core/proto_handler.h).
 *
 * One tick drains every pending event in a single bounded loop, so the worst case is
 * O(queue_depth + MAX_CONNS) rather than unbounded in the arrival rate.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SESSION_H
#define PROTOCORE_SESSION_H

#include "network_drivers/transport/tcp/evt.h" // EvtType, TcpEvt: the events this layer drains
#include "protocore_config.h"                  // CONN_POOL_SLOTS, proto_bool: the tables below

#include "server/core/proto_handler.h" // ProtoRegistryNs: carried below as Session.proto
#include "server/core/worker.h"        // WorkerNs: carried below as Session.workers

/**
 * @brief Per-connection state, keyed on the transport slot index.
 *
 * A connection is opened, closed and controlled here, so what a connection carries between its
 * requests is held here too rather than by whichever layer happens to read it. Sized
 * CONN_POOL_SLOTS, not MAX_CONNS: the HTTP/3 dispatch slot is a reserved index above the TCP range.
 * All BSS. Defined in session.c.
 *
 * @var http_req_start_ms  protocore_millis() at the first byte of the in-progress request (0 = none).
 *                         The request-header deadline (PROTOCORE_REQUEST_TIMEOUT_MS, slow-loris
 *                         defense) measures against this; unlike the transport's idle timer a
 *                         trickle byte cannot reset it.
 * @var http_resp_sink     where a response for the slot is written, per slot.
 */
typedef proto_bool (*protocore_resp_sink_fn)(uint8_t slot, int code, const char *content_type, const char *body,
                                             size_t len);

extern uint32_t http_req_start_ms[CONN_POOL_SLOTS];
extern protocore_resp_sink_fn http_resp_sink[CONN_POOL_SLOTS];

#if PROTOCORE_ENABLE_HTTP2
/**
 * @brief Whether the slot negotiated HTTP/2 (ALPN "h2"), whether that check has run, and the
 * stream it is serving.
 *
 * RFC 9113 sec 5: a stream is "an independent, bidirectional sequence of frames exchanged between
 * the client and server within an HTTP/2 connection", and "stream identifiers are assigned to
 * streams by the endpoint initiating the stream" - so which stream a slot is answering on is a
 * property of the connection, not of the request being parsed.
 */
extern uint8_t http_h2[CONN_POOL_SLOTS];
extern uint8_t http_h2_checked[CONN_POOL_SLOTS];
extern uint32_t http_h2_stream[CONN_POOL_SLOTS];
#endif

#if PROTOCORE_ENABLE_HTTP3
/** @brief The reserved HTTP/3 dispatch slot, and the QUIC connection and stream a response routes back on. */
extern uint8_t http_h3[CONN_POOL_SLOTS];
extern uint32_t http_h3_conn_id[CONN_POOL_SLOTS];
extern uint64_t http_h3_stream[CONN_POOL_SLOTS];
#endif

#if PROTOCORE_ENABLE_FILE_SERVING
/**
 * @brief A file transfer in progress on a slot: the open file and how much of it is left.
 *
 * A file larger than the send window cannot go out in one dispatch, so the transfer spans several
 * loops and is therefore something the CONNECTION carries between them, not something the file
 * server holds on the side. Session opens and closes the connection, so session holds it.
 */
typedef struct
{
    int fh;            ///< accessor handle for the open source file, held across loops.
    size_t off;        ///< absolute file offset of the next byte to send.
    size_t remaining;  ///< body bytes still to send.
    int status;        ///< response status (200 / 206) for note_response.
    int total;         ///< total body length, for the access log.
    proto_bool keep;   ///< keep-alive vs close at completion.
    proto_bool active; ///< a transfer is in progress on this slot.
} FileSend;

extern FileSend file_send[CONN_POOL_SLOTS];
#endif

#if PROTOCORE_ENABLE_SSH_SCP
/**
 * @brief One SCP transfer in progress on a slot: where it is in the rcp SINK exchange, the
 * destination it is writing, and how much of the file is still to arrive.
 *
 * A transfer spans many channel messages, so it is what the CONNECTION carries between them.
 * Session opens and closes the connection, so session holds it.
 */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_NONE,
    WAIT_CLINE, ///< reading the C<mode> <size> <name> control line
    RECV,       ///< streaming file data to disk
    WAIT_END    ///< the file's bytes are in; awaiting the end-of-record byte
} ScpSt;

typedef struct
{
    proto_bool active;
    uint8_t slot;
    uint32_t channel;
    ScpSt st;
    char dest[PROTOCORE_FILESYSTEM_PATH_MAX]; ///< the -t target (a file, or a dir if it ends with '/')
    proto_bool dest_is_dir;
    int fh;             ///< open file handle, or -1
    uint64_t remaining; ///< data bytes still to receive
    proto_bool err;
    uint16_t cl_len; ///< control-line accumulator length
    char cl[PROTOCORE_FILESYSTEM_PATH_MAX + 64];
} ScpConn;

extern ScpConn scp_conns[MAX_SSH_CONNS];
#endif

#if PROTOCORE_ENABLE_SSH_SFTP
/**
 * @brief One SFTP session on a slot: its open handles, a streaming write part way through, and
 * the accumulator holding an incomplete request packet.
 *
 * All of it spans several channel messages, so it is what the CONNECTION carries between them.
 * Session opens and closes the connection, so session holds it.
 */
typedef struct
{
    proto_bool is_dir;
    int fh;                                  ///< the accessor's handle (a dir cursor when is_dir)
    char req[PROTOCORE_FILESYSTEM_PATH_MAX]; ///< the request path this was opened with; FSTAT stats it
    proto_bool readdir_done;                 ///< the directory has been fully listed
    proto_bool has_pending;                  ///< a READDIR entry that did not fit last time, emitted first next time
    uint16_t pend_len;
    uint8_t pend[PROTOCORE_SFTP_ENTRY_MAX];
} SftpHandle;

typedef struct
{
    proto_bool active;
    uint8_t slot;
    uint32_t channel;
    // Which handles are open, one bit each. In-use state is a single bit, so the whole table's
    // answer fits in a register: allocation is one bit scan instead of a walk, releasing them all
    // touches only the ones actually open, and resetting the table is a store rather than a loop.
    uint32_t open_mask;
    uint16_t acc_len; ///< bytes accumulated toward the next request packet
    uint8_t acc[PROTOCORE_SFTP_PKT_BUF];
    // streaming write: a WRITE whose data payload arrives across CHANNEL_DATA calls
    proto_bool writing;
    int wr_handle;
    uint64_t wr_off;
    uint32_t wr_remaining;
    uint32_t wr_id;
    proto_bool wr_err;
    SftpHandle handles[PROTOCORE_SFTP_MAX_HANDLES];
} SftpSession;

extern SftpSession sftp_sess[MAX_SSH_CONNS];
#endif

/**
 * @brief The session tick, and the core modules it drives.
 *
 * A caller sets the members a call takes and invokes it through ::Session.
 *
 * @var SessionNs::worker_id  which worker is turning: whose slots it sweeps and whose queue it drains
 * @var SessionNs::conn_timeout_ms
 *                            milliseconds of inactivity before a connection is closed, applied by
 *                            the sweep this layer drives
 * @var SessionNs::tick       drive the layer for one loop iteration: sweep, drain, dispatch
 * @var SessionNs::proto      the protocol registry a connection is dispatched through
 * @var SessionNs::workers    the worker tasks that turn the pipeline, their deferred-callback
 *                            path, and the queue they jump when one is compiled in
 *
 * A child is a pointer: a table in one translation unit is not a constant expression in another.
 * A child behind a feature flag is declared under it, so the layer names only what the image
 * contains.
 */
typedef struct
{
    int worker_id;
    proto_u32 conn_timeout_ms;

    void (*const tick)(uint8_t *restrict work);
    ProtoRegistryNs *proto;
    WorkerNs *workers;
} SessionNs;

/** @brief The one symbol this module exports. */
extern SessionNs Session;

/**
 * @brief The PROTOCORE_SESSION_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_session_span(void);

#endif
