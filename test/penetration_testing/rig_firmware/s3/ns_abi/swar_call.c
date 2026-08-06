// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A consumer of the converted swar module, in its own TU. This is the shape that matters for the
// access layer: the lane tests inside a caller's word loop, which is what base64's classifier does.
// Linked against src/shared_primitives/swar.c and disassembled, it shows whether the members inline
// into the loop under LTO or cost an indirect call per lane test.

#include "mmgr/swar.h"

static char buf[64] __attribute__((aligned(8)));
volatile unsigned long swar_sink = 0;

void app_main(void);
void app_main(void)
{
    unsigned long n = 0;
    for (size_t i = 0; i + PC_SWAR_BYTES <= sizeof(buf); i += PC_SWAR_BYTES)
    {
        pc_swar_word w = swar.load_al(buf + i);
        pc_swar_word m = swar.ge(w, 'a') & swar.le(w, 'z');
        if (m != 0)
        {
            n += swar.zero_lane(m);
        }
    }
    swar_sink = n;
}
