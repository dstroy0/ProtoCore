// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// S3 swar-microbench sketch anchor. The bench itself is the SHARED ../../src/main_swarbench.cpp
// (setup()/loop() + the CCOUNT sweep and the CHECK pairs against newlib); build_s3_swarbench.sh
// stages that file into this sketch dir before compiling with arduino-cli. This .ino defines nothing.
//
// Purpose: decide on the die whether mmgr/swar.h's lane math beats the newlib string
// routines it replaces. The host answer is not evidence - an x86 host measures these against an AVX2
// libc on a core with more load ports than the loop can use, and it ranked a one-load-per-step
// routine BELOW an nlen-load one. An LX7 has one load port, no reordering to hide the extra loads,
// and a byte-loop strnlen. The two machines cannot both be right.
//
// Deliberately NO `#include "protocore.h"`. The sibling benches pull a root header in so arduino-cli's
// dependency finder attaches the library by name, but that also drags every other header into the
// build - and this branch is mid C-conversion, so transport/tcp.h reaches mmgr/ring.h
// whose _Atomic does not compile as C++. The bench needs mmgr/swar.h and nothing else,
// so the build script puts src/ on the include path directly and never attaches the library at all.
