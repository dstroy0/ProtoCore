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
# The sources are the ones MMgr's src/CMakeLists.txt enters, module for module. A directory it does
# not enter is not part of MMgr's build, and impensa_ancorae_acus compiles one profile of five, the
# one MMGR_ANCORAE_FORMA names and generic when nothing does, since each defines the same table.
#
# The include roots are the three MMgr's mmgr_add_module() gives a module: include/ for mmgr.h,
# src/ so a module header is reached as <module>/<module>.h, and embedded_types, which mmgr.h
# includes. The capability switches are MMgr's defaults, stated so every translation unit that reads
# them sees the same value.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/src")
  if(DEFINED MMGR_ANCORAE_FORMA)
    string(TOLOWER "${MMGR_ANCORAE_FORMA}" _pc_mmgr_forma)
  else()
    set(_pc_mmgr_forma generic)
  endif()
  set(_pc_mmgr_src "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/src")
  add_library(pc_mmgr OBJECT
    "${_pc_mmgr_src}/clz/clz.c"
    "${_pc_mmgr_src}/impensa_ancorae_acus/impensa_ancorae_acus_${_pc_mmgr_forma}.c"
    "${_pc_mmgr_src}/ascii_persona_bitorum/ascii_persona_bitorum.c"
    "${_pc_mmgr_src}/bitorum_introitus_exitus/bitorum_introitus_exitus.c"
    "${_pc_mmgr_src}/endian/endian.c"
    "${_pc_mmgr_src}/proximus_operor/proximus_operor.c"
    "${_pc_mmgr_src}/verbum_scrutor/verbum_scrutor.c"
    "${_pc_mmgr_src}/memoria_operor/memoria_operor.c"
    "${_pc_mmgr_src}/cellularum_laboro/cellularum_laboro.c"
    "${_pc_mmgr_src}/spatium/spatium.c"
    "${_pc_mmgr_src}/octetus_introitus_exitus/octetus_introitus_exitus.c"
    "${_pc_mmgr_src}/locus_carcerum/locus_carcerum.c"
    "${_pc_mmgr_src}/memoria_anularis/memoria_anularis.c"
    "${_pc_mmgr_src}/memoria_externa/memoria_externa.c"
    "${_pc_mmgr_src}/transformo/transformo.c"
    "${_pc_mmgr_src}/memoriam_praetereo/memoriam_praetereo.c"
    "${_pc_mmgr_src}/memoriam_praetereo/praet_ordo.c"
    "${_pc_mmgr_src}/memoriam_praetereo/praet_descriptor.c"
    "${_pc_mmgr_src}/fractio/fractio.c"
    "${_pc_mmgr_src}/verba_scribo/verba_scribo.c"
    "${_pc_mmgr_src}/numeros_scribo/numeros_scribo.c")
  target_include_directories(pc_mmgr PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded_types/include")
  target_compile_definitions(pc_mmgr PUBLIC MMGR_ENABLE_DMA=0 MMGR_ENABLE_EXTRAM=0)
endif()
