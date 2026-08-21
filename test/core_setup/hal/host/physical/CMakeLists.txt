# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The physical layer, answered by a loopback. Its own module rather than part of the host arm above:
# 13 envs take it and 204 take the arm, so a suite that needs a wire is saying something narrower
# than "run on the host".

protocore_add_module(test/core_setup/hal/host/physical
  SOURCES
    physical_mock.c
  DEPS
    network_drivers/physical/physical
)
