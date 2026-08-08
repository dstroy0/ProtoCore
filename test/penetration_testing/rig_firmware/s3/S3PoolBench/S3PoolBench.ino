// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// S3 memory-manager microbench sketch anchor. The bench itself is the SHARED ../../src/main_poolbench.cpp
// (setup()/loop() + the CCOUNT sweep); build_s3_poolbench.sh stages that file into this sketch dir
// before compiling with arduino-cli. This .ino defines nothing.
//
// Purpose: put numbers on the two pools - borrow cost, the cost of the ownership test that decides
// which pool a pointer belongs to, and, the one that matters, what the secure pool's wipe-on-release
// costs. Both pools are the same mechanism, so releasing equal byte counts from each isolates the
// wipe exactly. Until this runs, "virtually no overhead" is a hypothesis.
//
// The include below is a ROOT library header (src/protocore.h): arduino-cli's dependency finder only
// indexes a library's top-level headers to identify it, so a sketch whose only library includes are
// subdir paths (server/mmgr/...) never attaches the library. Pulling one root header in puts the
// whole src/ tree on the include path for every file in this build - including the staged bench source.
#include "protocore.h"
