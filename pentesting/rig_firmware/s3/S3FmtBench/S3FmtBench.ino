// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// S3 formatting-microbench sketch anchor. The bench itself is the SHARED ../../src/main_fmtbench.cpp
// (setup()/loop() + the CCOUNT sweep and the byte-identity CHECK pairs); build_s3_fmtbench.sh stages
// that file into this sketch dir before compiling with arduino-cli. This .ino defines nothing.
//
// Purpose: settle SRCBANNED ban 20's claim that a runtime-parsed format string costs materially more
// than building the frame from a pre-decoded spec. Until this runs, that ratio is a hypothesis.
//
// The include below is a ROOT library header (src/protocore.h): arduino-cli's dependency finder only
// indexes a library's top-level headers to identify it, so a sketch whose only library includes are
// subdir paths (shared_primitives/...) never attaches the library. Pulling one root header in puts the
// whole src/ tree on the include path for every file in this build - including the staged bench source.
#include "protocore.h"
