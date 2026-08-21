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
