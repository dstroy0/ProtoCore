# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# One module: a .c under src/ and the .h beside it.
#
# A module directory says WHAT IT IS and WHAT IT NEEDS, and nothing about how it is built:
#
#   protocore_add_module(crypto/hash/sha256
#     GATE PROTOCORE_ENABLE_SHA256
#     SOURCES sha256.c
#     DEPS crypto/crypto_opt mmgr/endian mmgr/protomem)
#
#   protocore_add_module(mmgr/span SOURCES span.c)            # no gate: substrate, always built
#   protocore_add_module(crypto/x509/x509_types HEADER_ONLY)  # no .c beside the .h
#
# WHY THE GATE IS DECLARED AND NOT INFERRED. It used to be read out of the .c by a generator whose
# pattern was `^#if PROTOCORE_ENABLE_\w+$` - one macro, nothing else on the line. A module wrapped in
# `#if PROTOCORE_TLS_SOFTWARE` or `#if PROTOCORE_ENABLE_OTA && PROTOCORE_HAS_VENDOR_OTA` therefore
# had NO gate as far as the build was concerned, so it was compiled into every target and the
# preprocessor threw the body away afterwards. Nothing reported it: the build was green, the binary
# was correct, and the module was in it. Saying the gate here cannot go quietly wrong that way - a
# module either names one or is substrate, and both are visible in its own directory.
#
# DEPS name MODULES BY PATH, not targets. The target name is derived here, so a module never spells
# `pc_mmgr_protomem` and cannot depend on something that is not a module.

# The target that builds module <path>: its path with the separators flattened.
#
# Path-derived rather than the directory's own name, because six of them are not unique across the
# tree - auth, client, inflate, network, server and sse each name more than one module, and the
# nesting by OSI layer is what tells them apart.
function(protocore_module_target path out)
  string(REPLACE "/" "_" _t "${path}")
  set(${out} "pc_${_t}" PARENT_SCOPE)
endfunction()

function(protocore_add_module path)
  cmake_parse_arguments(ARG "HEADER_ONLY" "GATE;OPT" "SOURCES;DEPS;PRIVATE_DEPS" ${ARGN})

  # A gate that is off is the module not existing. Declared here so the module's own directory is
  # the one place that says whether the build has it.
  if(ARG_GATE)
    if(NOT ${ARG_GATE})
      return()
    endif()
  endif()

  protocore_module_target("${path}" target)

  if(ARG_HEADER_ONLY)
    # No .c to compile, but it still carries its include directories and its dependencies, and a
    # consumer still links it.
    add_library(${target} INTERFACE)
    set(scope INTERFACE)
  else()
    set(_srcs "")
    foreach(s IN LISTS ARG_SOURCES)
      list(APPEND _srcs "${CMAKE_CURRENT_SOURCE_DIR}/${s}")
    endforeach()
    # OBJECT: a module contributes its objects to whatever links it - the same single link a flat
    # source list produced - while still carrying its includes and dependencies as usage
    # requirements. The header graph is acyclic, which is what OBJECT requires.
    add_library(${target} OBJECT ${_srcs})
    set(scope PUBLIC)
  endif()

  target_link_libraries(${target} ${scope} pc_config)

  # The -O level for this module's own translation units. Stated here rather than as a
  # `#pragma GCC optimize` in the sources: GCC documents that pragma as not for production, it
  # interacts badly with inlining, and this tree builds with -flto. Meaningless without sources.
  if(ARG_OPT AND NOT ARG_HEADER_ONLY)
    target_compile_options(${target} PRIVATE -O${ARG_OPT})
  endif()

  # PUBLIC, so a consumer that links this module also gets these headers and their own dependencies
  # without naming them.
  foreach(dep IN LISTS ARG_DEPS)
    protocore_module_target("${dep}" _d)
    target_link_libraries(${target} ${scope} ${_d})
  endforeach()

  # PRIVATE is for a module this one calls but does not expose in its own header, so a consumer does
  # not inherit it. Meaningless on an INTERFACE target, which has no compilation of its own.
  if(ARG_PRIVATE_DEPS AND NOT ARG_HEADER_ONLY)
    foreach(dep IN LISTS ARG_PRIVATE_DEPS)
      protocore_module_target("${dep}" _d)
      target_link_libraries(${target} PRIVATE ${_d})
    endforeach()
  endif()

  # Collected so an aggregate can link every module that survived its gate without listing them,
  # and so `build modules --check` can compare what was declared against what is on disk.
  set_property(GLOBAL APPEND PROPERTY PROTOCORE_MODULE_TARGETS ${target})
  set_property(GLOBAL APPEND PROPERTY PROTOCORE_MODULE_PATHS "${path}")
endfunction()
