# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The directories below. A new module is a new directory and a line here - nothing central holds a
# list of modules, so there is nothing else to keep in step.
#
# One target per module - a .c under src/ and the .h beside it. The dependencies are read out of what
# each module includes, resolved back to whichever module owns that header, so nothing here is
# maintained by hand.
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

# Every feature flag, and the -D that carries each one to the compiler. Read here, before the module
# directories below: protocore_add_module() returns early when its GATE is off, so the options have
# to be settled before the first module asks. Needs pc_config, which is why it is not higher up.
include(ProtoCoreOptions)

# One module. DEPS are other modules; each is PUBLIC, so a consumer that links this one also gets
# their headers and their own dependencies without naming them.
function(protocore_module name)
  cmake_parse_arguments(M "" "" "SOURCES;PUBLIC;PRIVATE" ${ARGN})
  add_library(${name} OBJECT ${M_SOURCES})
  target_link_libraries(${name} PUBLIC pc_config ${M_PUBLIC})
  if(M_PRIVATE)
    target_link_libraries(${name} PRIVATE ${M_PRIVATE})
  endif()
endfunction()

# A module with no .c beside its .h: header-only, and a consumer still depends on it. INTERFACE, so
# it contributes no objects but carries its own dependencies onward exactly as a compiled one does.
function(protocore_header_module name)
  cmake_parse_arguments(M "" "" "DEPS" ${ARGN})
  add_library(${name} INTERFACE)
  target_link_libraries(${name} INTERFACE pc_config ${M_DEPS})
endfunction()

add_subdirectory(crypto)
add_subdirectory(mmgr)
add_subdirectory(network_drivers)
add_subdirectory(protocore)
add_subdirectory(server)
add_subdirectory(services)
add_subdirectory(shared)
