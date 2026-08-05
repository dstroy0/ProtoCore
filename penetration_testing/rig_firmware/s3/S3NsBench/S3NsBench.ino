// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Does a namespace struct cost anything? Built twice from one source (see ../build_s3_nsbench.sh):
// PC_NS_FORM=0 calls three of twenty-four leaves by name, PC_NS_FORM=1 calls the same three through
// Network.auth.login / Network.tls.handshake / Server.signaling.peek, with each group's opaque
// context carried as a member of its own namespace struct.
//
// The number that decides it is flash: the linker strips a leaf nothing references, and a const
// initializer naming all twenty-four is a reference to all twenty-four. If both images are the same
// size the table is free; if the second is larger by the twenty-one unused leaves plus the pointers,
// it is not.

#ifndef PC_NS_FORM
#define PC_NS_FORM 0
#endif

volatile uint32_t sink = 0;

// Each leaf is the same shape and distinct constant, so none folds into another and none is empty.
#define LEAF(n)                                                                                                        \
    static void f##n(uint32_t v)                                                                                       \
    {                                                                                                                  \
        sink += (v ^ 0x##n##u) * 2654435761u;                                                                          \
        sink ^= sink >> 13;                                                                                            \
        sink += v * 40503u;                                                                                            \
    }

LEAF(00) LEAF(01) LEAF(02) LEAF(03) LEAF(04) LEAF(05) LEAF(06) LEAF(07)
LEAF(08) LEAF(09) LEAF(10) LEAF(11) LEAF(12) LEAF(13) LEAF(14) LEAF(15)
LEAF(16) LEAF(17) LEAF(18) LEAF(19) LEAF(20) LEAF(21) LEAF(22) LEAF(23)

#if PC_NS_FORM

// The owned context each concern already has (SRCBANNED #12), reached as a member of the namespace
// struct rather than as a file-scope name.
typedef struct
{
    uint32_t tries;
} AuthCtx;
typedef struct
{
    uint32_t epoch;
} TlsCtx;
typedef struct
{
    uint32_t depth;
} SignalCtx;

static AuthCtx s_auth = {0};
static TlsCtx s_tls = {0};
static SignalCtx s_signal = {0};

typedef void (*leaf_fn)(uint32_t);

typedef struct
{
    AuthCtx *ctx;
    leaf_fn login;
    leaf_fn logout;
    leaf_fn refresh;
    leaf_fn revoke;
    leaf_fn probe;
    leaf_fn reset;
    leaf_fn begin;
    leaf_fn on;
} AuthNs;

typedef struct
{
    TlsCtx *ctx;
    leaf_fn handshake;
    leaf_fn close;
    leaf_fn rekey;
    leaf_fn verify;
    leaf_fn pin;
    leaf_fn stats;
    leaf_fn begin;
    leaf_fn on;
} TlsNs;

typedef struct
{
    SignalCtx *ctx;
    leaf_fn peek;
    leaf_fn post;
    leaf_fn drain;
    leaf_fn arm;
    leaf_fn clear;
    leaf_fn count;
    leaf_fn begin;
    leaf_fn on;
} SignalNs;

typedef struct
{
    AuthNs auth;
    TlsNs tls;
} NetworkNs;
typedef struct
{
    SignalNs signaling;
} ServerNs;

static const NetworkNs Network = {
    {&s_auth, f00, f01, f02, f03, f04, f05, f06, f07},
    {&s_tls, f08, f09, f10, f11, f12, f13, f14, f15},
};
static const ServerNs Server = {
    {&s_signal, f16, f17, f18, f19, f20, f21, f22, f23},
};

#endif // PC_NS_FORM

void setup()
{
    Serial.begin(115200);
#if PC_NS_FORM
    Network.auth.login(1);
    Network.tls.handshake(2);
    Server.signaling.peek(3);
    Serial.printf("NS form   ctx=%u/%u/%u sink=%u\n", Network.auth.ctx->tries, Network.tls.ctx->epoch,
                  Server.signaling.ctx->depth, sink);
#else
    f00(1);
    f08(2);
    f16(3);
    Serial.printf("FLAT form sink=%u\n", sink);
#endif
}

void loop()
{
}
