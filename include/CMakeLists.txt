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
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/CMakeLists.txt")
  add_subdirectory(MMgr "${CMAKE_CURRENT_BINARY_DIR}/mmgr")
endif()
