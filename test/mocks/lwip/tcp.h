#pragma once
#include <stdint.h>

typedef int8_t err_t;
typedef uint16_t u16_t;

#define ERR_OK ((err_t)0)
#define ERR_MEM ((err_t) - 1)
#define ERR_VAL ((err_t) - 6)
#define ERR_ABRT ((err_t) - 8)
#define ERR_USE ((err_t) - 9)

#define IPADDR_TYPE_ANY 0
#define IP_ANY_TYPE NULL
#define TCP_WRITE_FLAG_COPY 0x01

struct tcp_pcb
{
    // Outstanding TX segments not yet acked. The real lwIP pcb has this; the
    // transport's ConnState::CONN_CLOSING dwell reads it to know when our response has
    // drained. Defaults to 0 so a host close finalizes immediately unless a test
    // sets it to simulate data still in flight.
    uint16_t snd_queuelen = 0;
    // IPv4 TOS / DS field. The real lwIP pcb has this; DiffServ marking (PC_ENABLE_DIFFSERV) writes the
    // DSCP here so a host test can assert the applied class. Defaults to 0 (best-effort).
    uint8_t tos = 0;
};

struct pbuf
{
    void *payload;
    uint16_t len;
    uint16_t tot_len;
    struct pbuf *next;
};

// Return a stable non-null address so init() path succeeds in native tests.
// We use a single static instance since the mock never actually owns memory.
static struct tcp_pcb _mock_pcb;
// Typed callback aliases matching the real lwIP API so tcp.cpp compiles
// without modification.
typedef err_t (*tcp_accept_fn)(void *arg, struct tcp_pcb *newpcb, err_t err);
typedef err_t (*tcp_recv_fn)(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
typedef err_t (*tcp_sent_fn)(void *arg, struct tcp_pcb *tpcb, u16_t len);
typedef void (*tcp_err_fn)(void *arg, err_t err);

// Test hooks: force the next call to report failure, modeling the real-lwIP cases
// listener_add() guards against (out of PCBs / a port already bound / backlog alloc
// failure). Each auto-clears after one use.
inline proto_bool &mock_new_pcb_fail_once()
{
    static proto_bool v = PROTO_FALSE;
    return v;
}
inline proto_bool &mock_bind_fail_once()
{
    static proto_bool v = PROTO_FALSE;
    return v;
}
inline proto_bool &mock_listen_fail_once()
{
    static proto_bool v = PROTO_FALSE;
    return v;
}
inline struct tcp_pcb *tcp_new_ip_type(int)
{
    if (mock_new_pcb_fail_once())
    {
        mock_new_pcb_fail_once() = PROTO_FALSE;
        return NULL;
    }
    return &_mock_pcb;
}
inline err_t tcp_bind(struct tcp_pcb *, void *, uint16_t)
{
    if (mock_bind_fail_once())
    {
        mock_bind_fail_once() = PROTO_FALSE;
        return ERR_USE; // port already bound
    }
    return ERR_OK;
}
inline struct tcp_pcb *tcp_listen_with_backlog(struct tcp_pcb *p, uint8_t)
{
    if (mock_listen_fail_once())
    {
        mock_listen_fail_once() = PROTO_FALSE;
        return NULL;
    }
    return p;
}
inline void tcp_arg(struct tcp_pcb *, void *)
{
}
inline void tcp_nagle_disable(struct tcp_pcb *)
{
}
inline void tcp_accept(struct tcp_pcb *, tcp_accept_fn)
{
}
inline void tcp_recv(struct tcp_pcb *, tcp_recv_fn)
{
}
inline void tcp_sent(struct tcp_pcb *, tcp_sent_fn)
{
}
inline void tcp_err(struct tcp_pcb *, tcp_err_fn)
{
}
// Call counter: lets a test prove tcp_abort() was (or was not) reached - e.g. the
// tcp_close()-failed fallback path, which has no other observable side effect.
inline int &mock_abort_call_count()
{
    static int v = 0;
    return v;
}
inline void tcp_abort(struct tcp_pcb *)
{
    mock_abort_call_count()++;
}
// ---------------------------------------------------------------------------
// Optional write capture - off by default; tests enable with tcp_capture_reset()
// ---------------------------------------------------------------------------

struct TcpCapture
{
    char buf[65536]; // large enough to capture a multi-window file response whole
    size_t len;
};

inline proto_bool &_tcp_capture_active()
{
    static proto_bool v = PROTO_FALSE;
    return v;
}

inline TcpCapture &_tcp_capture()
{
    static TcpCapture c = {0};
    return c;
}

// Test hook: after this many successful writes, tcp_write fails (ERR_MEM, nothing
// queued) - models a full/transient TCP send buffer so pc_conn_send() returns false
// and a send pump takes its un-read-and-retry path. -1 (default) never fails.
inline int &mock_send_fail_after()
{
    static int v = -1;
    return v;
}

inline void tcp_capture_reset()
{
    _tcp_capture().len = 0;
    _tcp_capture().buf[0] = '\0';
    _tcp_capture_active() = PROTO_TRUE;
    mock_send_fail_after() = -1; // clear a send-failure a prior test may have armed
}

inline void tcp_capture_disable()
{
    _tcp_capture_active() = PROTO_FALSE;
}

inline const char *tcp_captured()
{
    return _tcp_capture().buf;
}
inline size_t tcp_captured_len()
{
    return _tcp_capture().len;
}

inline err_t tcp_write(struct tcp_pcb *, const void *data, uint16_t len, uint8_t)
{
    int &fa = mock_send_fail_after();
    if (fa == 0)
    {
        return ERR_MEM; // send buffer full: nothing queued, pc_conn_send() -> false
    }
    if (fa > 0)
    {
        fa--; // count down the writes allowed before the failure
    }
    if (_tcp_capture_active())
    {
        TcpCapture &c = _tcp_capture();
        size_t avail = sizeof(c.buf) - c.len - 1;
        size_t n = (len < avail) ? (size_t)len : avail;
        if (n > 0)
        {
            memcpy(c.buf + c.len, data, n);
            c.len += n;
            c.buf[c.len] = '\0';
        }
    }
    return ERR_OK;
}
inline void tcp_output(struct tcp_pcb *)
{
}
// Advisory send window for the flow-control query. A real window shrinks as bytes go
// unacked and reopens on ACK, which is what drives the file/chunk send pumps onto their
// backpressure-and-resume path across worker loops. A test can shrink this (mock_sndbuf()
// = 0) to force that path, then reopen it before the next poll to resume the transfer.
static const uint16_t MOCK_SNDBUF_DEFAULT = 5744; // a typical lwIP TCP_SND_BUF
inline uint16_t &mock_sndbuf()
{
    static uint16_t v = MOCK_SNDBUF_DEFAULT;
    return v;
}
inline uint16_t tcp_sndbuf(struct tcp_pcb *)
{
    return mock_sndbuf();
}
// Test hook: force the next tcp_close() call to report failure, modeling the rare real-lwIP case
// where the graceful close cannot complete and the caller falls back to tcp_abort(). Auto-clears
// after one use so a test cannot leave it armed for whatever runs after it.
inline proto_bool &mock_close_fail_once()
{
    static proto_bool v = PROTO_FALSE;
    return v;
}
inline err_t tcp_close(struct tcp_pcb *)
{
    if (mock_close_fail_once())
    {
        mock_close_fail_once() = PROTO_FALSE;
        return ERR_MEM; // arbitrary non-OK; every caller only checks != ERR_OK
    }
    return ERR_OK;
}
inline void tcp_recved(struct tcp_pcb *, uint16_t)
{
}
inline void pbuf_free(struct pbuf *)
{
}
