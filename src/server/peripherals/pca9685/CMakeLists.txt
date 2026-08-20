# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/peripherals/pca9685
  GATE PROTOCORE_ENABLE_PCA9685
  SOURCES pca9685.c
  PRIVATE_DEPS
    mmgr/secure
    server/clock
    server/peripherals/i2c
)
