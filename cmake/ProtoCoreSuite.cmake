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
  # main() say OWN_MAIN. It is written here, at configure time, from the Unity that
  # cmake/ProtoCoreDeps.cmake fetched, through the same `harness.py runners gen` a person runs by
  # hand - so its refusals (a case the generator would walk past) stop the configure by name.
  #
  # Configure time and not a build rule: the runner is an INTERFACE source compiled by env targets
  # in another directory, and a custom command's OUTPUT is only built for targets in its own. The
  # suite's sources are configure dependencies, so adding a case reruns this on the next build.
  if(NOT ARG_OWN_MAIN)
    set(_runner "${CMAKE_CURRENT_SOURCE_DIR}/unity_runner.c")
    set(_stale FALSE)
    if(NOT EXISTS "${_runner}")
      set(_stale TRUE)
    endif()
    foreach(s IN LISTS ARG_SOURCES)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${s}")
      if("${CMAKE_CURRENT_SOURCE_DIR}/${s}" IS_NEWER_THAN "${_runner}")
        set(_stale TRUE)
      endif()
    endforeach()
    if(_stale)
      execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${PROTOCORE_ROOT}/test/harness.py" runners gen
                "${CMAKE_CURRENT_SOURCE_DIR}" --unity "${PROTOCORE_UNITY_RB}"
        WORKING_DIRECTORY "${PROTOCORE_ROOT}"
        RESULT_VARIABLE _rc
        OUTPUT_QUIET
        ERROR_VARIABLE _err)
      if(NOT _rc EQUAL 0 OR NOT EXISTS "${_runner}")
        message(FATAL_ERROR "suite ${path}: could not generate unity_runner.c\n${_err}")
      endif()
    endif()
    target_sources(${target} INTERFACE "${_runner}")
  endif()

  # A suite includes its own fixtures by bare name.
  target_include_directories(${target} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

  # Collected so `build suites --check` can compare what is declared against what is on disk.
  set_property(GLOBAL APPEND PROPERTY PROTOCORE_SUITE_PATHS "${path}")
endfunction()
