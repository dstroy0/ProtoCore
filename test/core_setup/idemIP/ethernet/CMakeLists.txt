# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The one thing in idemIP that is not just a layout: the MII register set has a bring-up sequence -
# reset, autoneg, link poll - so phy.c holds it. Everything it needs is the layouts above it.

protocore_add_module(test/core_setup/idemIP/ethernet
  SOURCES
    phy.c
  DEPS
    test/core_setup/idemIP
)
