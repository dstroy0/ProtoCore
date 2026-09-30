# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The single library entry. `#include <protocore.h>` is what a user writes, and nothing else in this
# tree is a public include path.
#
# Header-only, and it depends on nothing: it is the umbrella that names the modules a build selected,
# and CMake is what selected them. Twenty modules under src/ include it, which is why it is a module
# rather than a loose header - a target they can depend on rather than a path they happen to reach.

protocore_add_module(include
  HEADER_ONLY
)

# MMgr is a separate repository, vendored as a submodule. Its own build description is not entered:
# it builds MMgr once per width environment for MMgr's suites, and it reaches its include root
# through CMAKE_SOURCE_DIR, which names this tree's top rather than MMgr's once it is a
# subdirectory. ProtoCore builds MMgr's sources as one more target under ProtoCore's own flags, as
# the Arduino, PlatformIO and ESP-IDF builds do.
#
# The include roots are the three MMgr's mmgr_add_module() gives a module: include/ for mmgr.h,
# src/ so a module header is reached as <module>/<module>.h, and embedded_types, which mmgr.h
# includes. The capability switches are MMgr's defaults, stated so every translation unit that reads
# them sees the same value.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/src")
  file(GLOB_RECURSE PC_MMGR_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/src/*.c")
  add_library(pc_mmgr OBJECT ${PC_MMGR_SRCS})
  target_include_directories(pc_mmgr PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded_types/include")
  target_compile_definitions(pc_mmgr PUBLIC MMGR_ENABLE_DMA=0 MMGR_ENABLE_EXTRAM=0)
endif()
