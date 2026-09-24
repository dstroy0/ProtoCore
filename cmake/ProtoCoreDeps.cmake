# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The host build's third-party packages, fetched by CMake and pinned here - the one place they are
# named. Nothing under .pio/ is read: the native envs left platformio.ini, so `pio pkg install` has
# nothing to install them for, and a checkout that has never run pio still builds.
#
#   Unity     every env links unity.c, and generate_test_runner.rb writes each suite's runner.
#   littlefs  the filesystem the device runs; test/core_setup/hal/host/lfs_mock.h puts a real volume
#             on a RAM block device, so a host test asserts against real directory semantics.
#
# Unity's pin is MMgr's. include/MMgr declares the same package by the same name, and FetchContent
# keeps the FIRST declaration it sees - this one, because test/CMakeLists.txt includes this file
# before it enters include/. Two pins that disagree would build MMgr's suites against a Unity it
# never asked for, so they are kept equal rather than left to the ordering.
#
# Both come from git, so the first configure needs the network; after that they live under
# <build>/_deps and a reconfigure does not fetch again. An offline or air-gapped checkout points at
# a local copy instead:  -DFETCHCONTENT_SOURCE_DIR_UNITY=<dir> -DFETCHCONTENT_SOURCE_DIR_LITTLEFS=<dir>

include(FetchContent)

# The runners are written by test/harness.py, which drives Unity's generator (Ruby).
find_package(Python3 REQUIRED COMPONENTS Interpreter)

FetchContent_Declare(
  unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG        v2.6.1
  GIT_SHALLOW    TRUE
)
FetchContent_Declare(
  littlefs
  GIT_REPOSITORY https://github.com/littlefs-project/littlefs.git
  GIT_TAG        v2.11.3
  GIT_SHALLOW    TRUE
)

# Populated, not added. Unity's own CMakeLists builds a library with its configuration fixed, and an
# env has to compile unity.c under its own defines (UNITY_INCLUDE_DOUBLE is one); littlefs is only
# compiled into the envs whose suites reach lfs.h. Either way the sources are what is wanted, not a
# target. MMgr calls FetchContent_MakeAvailable(unity) later and adds Unity's library for itself;
# that is its build, and populating first does not stop it.
foreach(_dep unity littlefs)
  FetchContent_GetProperties(${_dep})
  if(NOT ${_dep}_POPULATED)
    FetchContent_Populate(${_dep})
  endif()
endforeach()

set(PROTOCORE_UNITY_DIR "${unity_SOURCE_DIR}/src" CACHE INTERNAL "Unity/src")
set(PROTOCORE_UNITY_RB "${unity_SOURCE_DIR}/auto/generate_test_runner.rb" CACHE INTERNAL "Unity's runner generator")
set(PROTOCORE_LITTLEFS_DIR "${littlefs_SOURCE_DIR}" CACHE INTERNAL "littlefs sources")
