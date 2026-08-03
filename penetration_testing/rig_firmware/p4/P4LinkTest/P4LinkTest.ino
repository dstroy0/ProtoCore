// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ESP32-P4 (Waveshare P4-POE-ETH) physical (L1) link bring-up test anchor: the twin of the S3 pio
// rig_s3_linktest, exercising the reorganized physical/esp backend's wired-Ethernet path on real silicon.
// The test body is the SHARED ../../src/main_linktest.cpp; build_p4_linktest.sh stages that file here before
// compiling with arduino-cli (the P4 needs the arduino-esp32 3.x core). build_opt.h sets PC_ENABLE_ETHERNET=1
// so main_linktest.cpp brings up the RMII PHY (init_eth_physical) instead of WiFi. This .ino is only the
// sketch anchor arduino-cli needs - it pulls one root library header (protocore.h) so the whole src/ tree,
// including the staged main_linktest.cpp, lands on the include path; setup()/loop() come from that file.
#include "protocore.h"
