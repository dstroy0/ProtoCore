# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The portable arm: the same seams answered in C the standard defines, with nothing of the host in
# them. This is the one most of the matrix takes - 204 envs name portable_platform.c against 10 for
# the host one - because a suite that is testing a protocol wants the platform to be uninteresting.

protocore_add_module(test/core_setup/hal/portable
  SOURCES
    portable_bignum.c
    portable_platform.c
  DEPS
    crypto/asymmetric/bignum
    mmgr/secure
)
