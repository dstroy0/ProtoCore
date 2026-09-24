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

# MMgr is a separate repository, vendored as a submodule, and brings its own build description. Its
# ROOT is the entry, not its src/: that is where mmgr_add_module() is defined, and every directory
# below it calls that. Entering src/ directly fails on the first module.
#
# ITS BINARY DIRECTORY IS ONE CHARACTER, and that is not tidiness. A Windows path is capped at 260
# characters, and MMgr's own deepest suite spends 211 of them before the object file:
# test/unit/confinium_exclusivum_infinitas/test_confinium_exclusivum_infinitas/CMakeFiles/
# test_confinium_exclusivum_infinitas_host.dir - the module name three times over. Two of that
# suite's sources live in test/support/ rather than beside it, so CMake namespaces them under a
# 32-character hash of their directory, and gcc then cannot create the .d file beside the object.
# Five targets failed to compile that way under `<build>/pc_include/mmgr`. Under `<build>/m` they
# fit. Nested one directory deeper and they would not, so this is load bearing.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/CMakeLists.txt")
  add_subdirectory(MMgr "${CMAKE_BINARY_DIR}/m")
endif()

# MMgr's mmgr_add_module() names its include/ and its host-traps header through ${CMAKE_SOURCE_DIR},
# which is MMgr's root only when MMgr is the top-level project. Entered from here it is ProtoCore's
# root, and every MMgr target points at a mmgr.h and a test/support/ that are not there. Rewrite
# those paths on MMgr's own targets to its source dir. A no-op once MMgr names ${MMgr_SOURCE_DIR}.
function(_protocore_mmgr_fix_paths dir)
  get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_t IN LISTS _targets)
    get_target_property(_type ${_t} TYPE)
    set(_props INTERFACE_INCLUDE_DIRECTORIES INTERFACE_COMPILE_OPTIONS)
    if(NOT _type STREQUAL "INTERFACE_LIBRARY")
      list(APPEND _props INCLUDE_DIRECTORIES COMPILE_OPTIONS)
    endif()
    foreach(_p IN LISTS _props)
      get_target_property(_v ${_t} ${_p})
      if(_v)
        set(_out)
        foreach(_e IN LISTS _v)
          if(_e STREQUAL "${CMAKE_SOURCE_DIR}/include")
            set(_e "${MMgr_SOURCE_DIR}/include")
          elseif(_e STREQUAL "${CMAKE_SOURCE_DIR}/test/support/mmgr_host_traps.h")
            set(_e "${MMgr_SOURCE_DIR}/test/support/mmgr_host_traps.h")
          endif()
          list(APPEND _out "${_e}")
        endforeach()
        set_target_properties(${_t} PROPERTIES ${_p} "${_out}")
      endif()
    endforeach()
  endforeach()
  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(_d IN LISTS _subdirs)
    _protocore_mmgr_fix_paths("${_d}")
  endforeach()
endfunction()
if(DEFINED MMgr_SOURCE_DIR AND NOT MMgr_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  _protocore_mmgr_fix_paths("${MMgr_SOURCE_DIR}")
endif()
