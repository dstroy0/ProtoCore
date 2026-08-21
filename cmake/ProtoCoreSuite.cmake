# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# One suite: a directory under test/unit or test/env holding a Unity test file.
#
# A suite directory says WHAT IT IS, and nothing about how it is built:
#
#   protocore_add_suite(unit/src/crypto/test_ct_eq SOURCES test_ct_eq.c)
#   protocore_add_suite(env/misc/test_bench OWN_MAIN SOURCES test_bench.c)
#
# WHY A SUITE IS AN INTERFACE LIBRARY AND NOT AN OBJECT ONE. The matrix compiles the same suite
# under more than one flag set - 59 of them are run by several envs, and test_connection by five -
# and 296 distinct define sets exist across the matrix as a whole. An OBJECT library is compiled
# once, so it could serve at most one of those. INTERFACE_SOURCES propagate to the consumer and are
# compiled AS the consumer, with the consumer's defines: the declaration is shared and the
# compilation is not, which is exactly the shape the matrix needs.
#
# WHY THE SOURCES ARE DECLARED AND NOT SCANNED. gen_cmake.py listed whatever .c files were in the
# directory at generation time. A suite whose directory it could not read produced no sources, and
# an empty suite is a target that is simply not written - the same silent shape as a module gate
# nothing evaluates. There is a comment in that generator about the day this collapsed 437 targets
# to 27 and wrote the result over the committed file without an error. A declaration cannot go
# quietly wrong that way: the file is named here, and it is missing or it is not.

# The target that carries suite <path>: its path with the separators flattened. `pcs_` and not
# `pc_`, so a suite and a module can never collide - unit/src/mmgr/test_span and mmgr/span would
# otherwise both want the tail of the same name.
function(protocore_suite_target path out)
  string(REPLACE "/" "_" _t "${path}")
  set(${out} "pcs_${_t}" PARENT_SCOPE)
endfunction()

function(protocore_add_suite path)
  cmake_parse_arguments(ARG "OWN_MAIN" "" "SOURCES" ${ARGN})

  protocore_suite_target("${path}" target)
  add_library(${target} INTERFACE)

  foreach(s IN LISTS ARG_SOURCES)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${s}")
      message(FATAL_ERROR "suite ${path} declares ${s}, which is not in ${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    target_sources(${target} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/${s}")
  endforeach()

  # Unity's runner - the file that calls RUN_TEST once per test - is GENERATED, not committed, so a
  # fresh checkout has none of them. 354 of the 376 suites need one; the 22 that write their own
  # main() say OWN_MAIN. Stopping here names the suite and the fix; without it the suite links with
  # no main() and the error is a linker's, pointing at nothing.
  if(NOT ARG_OWN_MAIN)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unity_runner.c")
      message(FATAL_ERROR
        "suite ${path} has no unity_runner.c and does not declare OWN_MAIN.\n"
        "  Generate the runners:  python test/harness.py runners gen")
    endif()
    target_sources(${target} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/unity_runner.c")
  endif()

  # A suite includes its own fixtures by bare name.
  target_include_directories(${target} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

  # Collected so `build suites --check` can compare what is declared against what is on disk.
  set_property(GLOBAL APPEND PROPERTY PROTOCORE_SUITE_PATHS "${path}")
endfunction()
