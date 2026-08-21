# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The wire formats, as headers: ethernet, ARP, IPv4, IPv6, ICMP, ICMPv6, TCP, UDP, and the checksum
# and byte-order accessors under them. Structure and arithmetic only - no state, no sockets, no
# definitions - which is why it is header-only and why a suite can include one frame layout without
# pulling in a stack.
#
# Included as "idemIP/<...>.h", from this directory's parent, so that path is carried as a usage
# requirement rather than left to whichever env happened to pass -Itest/core_setup.

protocore_add_module(test/core_setup/idemIP
  HEADER_ONLY
)

protocore_module_target(test/core_setup/idemIP _idemip)
target_include_directories(${_idemip} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_subdirectory(ethernet)
