// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The shape a real module has: the namespace struct is defined in the owning TU and reached from
// another one, so the caller sees `extern const` and not the initializer. ns_abi.c measured the
// same-TU case, where the initializer is visible and gcc folds it; this measures whether the fold
// survives the TU boundary, which is the case every consumer is in.
//
// Built twice from this one file (see ../build_s3_nsabi.sh): PC_NS_XTU=1 is the owning TU, =2 is the
// caller. Link the two and disassemble app_main.

#include <stdint.h>

typedef struct RadioCtx RadioCtx;

typedef struct
{
    RadioCtx *ctx;
    void (*power)(uint32_t);
    void (*ps_get)(uint32_t);
    void (*busy_hold)(uint32_t);
} RadioNs;

extern const RadioNs Radio;

#if PC_NS_XTU == 1

// The owning TU: storage and entry points have internal linkage, the struct instance is the only
// symbol that leaves.
struct RadioCtx
{
    uint32_t held;
};
static struct RadioCtx s_radio;

volatile uint32_t sink = 0;

static void __attribute__((noinline)) power(uint32_t v)
{
    sink += v * 2654435761u;
}
static void __attribute__((noinline)) ps_get(uint32_t v)
{
    sink ^= v * 40503u;
}
static void __attribute__((noinline)) busy_hold(uint32_t v)
{
    sink += v + 1u;
}

const RadioNs Radio = {&s_radio, power, ps_get, busy_hold};

#else

// The calling TU: it has the struct's layout from the header and the object by name, and nothing
// else. Whether these three become direct calls is the question.
void app_main(void);
void app_main(void)
{
    Radio.power(1);
    Radio.ps_mode(2);
    Radio.busy_hold(3);
}

#endif
