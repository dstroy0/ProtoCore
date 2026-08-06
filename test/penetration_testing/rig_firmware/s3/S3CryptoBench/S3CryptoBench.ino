// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// S3 crypto-microbench sketch anchor: the ESP32-S3 (Waveshare S3-POE-ETH) twin of rig_s3_cryptobench.
// The bench itself is the SHARED ../../src/main_cryptobench.cpp (setup()/loop() + the CCOUNT sweep and the
// RFC 8032 / RFC 6979 KATs); build_p4_cryptobench.sh stages that file and the RSA host-key fixture into this
// sketch dir before compiling with arduino-cli (the S3 needs the arduino-esp32 3.x core). This .ino is only
// the sketch anchor arduino-cli requires - it defines nothing; setup()/loop() come from main_cryptobench.cpp.
//
// Purpose: HW-verify the per-variant PC_FE25519_MPI_HW / PC_ECDSA_MPI_HW MODMULT field accel on the S3's
// RSA accelerator (hw_ver3 registers) - the KAT lines prove byte-exactness, the CB lines the speedup.
//
// The include below is a ROOT library header (src/protocore.h): arduino-cli's dependency finder only indexes a
// library's top-level headers to identify it, so a sketch whose only library includes are subdir paths
// (crypto/...) never attaches the library. Pulling one root header in puts the whole src/ tree on the include
// path for every file in this build - including the shared main_cryptobench.cpp staged alongside this anchor.
#include "protocore.h"
