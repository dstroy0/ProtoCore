// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A consumer of the converted protostr module, in its own TU. Linked against src/mmgr/protostr.c and
// disassembled, it shows whether str.eq() is a direct call under LTO and an indirect one without it,
// and whether the ci literal folds back through the struct.

#include "mmgr/protostr.h"

volatile unsigned char str_sink = 0;

void app_main(void);
void app_main(void)
{
    str_sink = (unsigned char)str.eq("Content-Length", "content-length", 15u, PROTO_TRUE);
}
