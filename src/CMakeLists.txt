# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# One directory per module, each declaring itself with protocore_add_module(): what it is, what
# gates it, and what it needs. Nothing here lists the modules, so adding one is a new directory
# and a line in its parent - there is no central register to keep in step.
#
# OBJECT libraries: a module contributes its objects to whatever links it, which is the same single
# link the flat source list produced, while still carrying its include directories and its
# dependencies as usage requirements. The header graph is acyclic, which is what OBJECT requires -
# `build modules --cycles` checks it, and only header edges can close a loop. Two .c files calling
# each other's published handle is ordinary C and is PRIVATE, so it is not part of that graph.

# Everything compiles against the entry point and the primitive types it assembles.
add_library(pc_config INTERFACE)
# The same set every env compiles with (tools/ci_tooling/build/gen_cmake.py BASE_INCLUDES): the host
# arm answers the platform seams in software, so a host build of the library reaches it the same way
# a suite does.
target_include_directories(pc_config INTERFACE
  "${PROTOCORE_ROOT}/test/core_setup/hal/host"
  "${PROTOCORE_ROOT}/test/support"
  "${PROTOCORE_ROOT}/src"
  "${PROTOCORE_ROOT}/include"
  "${PROTOCORE_ROOT}")


list(APPEND CMAKE_MODULE_PATH "${PROTOCORE_ROOT}/cmake")
include(ProtoCoreModule)
# Every feature flag and every dependency between them, declared once.
include(ProtoCoreOptions)

add_subdirectory(crypto)
add_subdirectory(mmgr)
add_subdirectory(network_drivers)
add_subdirectory(protocore)
add_subdirectory(server)
add_subdirectory(services)
add_subdirectory(shared)
