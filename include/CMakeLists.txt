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
    "${_pc_mmgr_src}/memoria_operor/memoria_linea.c"
    "${_pc_mmgr_src}/cellularum_laboro/cellularum_laboro.c"
    "${_pc_mmgr_src}/cellularum_laboro/cellularum_linea.c"
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
    "${_pc_mmgr_src}/verba_scribo/verba_linea.c"
    "${_pc_mmgr_src}/numeros_scribo/numeros_scribo.c")
  # This directory holds mmgr_settings.h, which mmgr.h and config/mmgr_config.h read from the include path.
  target_include_directories(pc_mmgr PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded_types/include")
  # On a MinGW target, the assembler writes the offset of a weak default in its section into each
  # reference to that weak name. The linker keeps that addend when it binds the name. Every
  # reference to a weak default off offset 0 thus reaches the wrong address. One section for each
  # function and each object puts every weak default at offset 0 of its own section.
  if(MINGW)
    target_compile_options(pc_mmgr PRIVATE -ffunction-sections -fdata-sections)
  endif()

  # A program defines the hooks that the turnstiles of embedded_types call: embed_word_wait,
  # embed_word_wake, embed_clock_ns and embed_thread_place. A device build gets them from its vendor
  # arm. This host build takes the definitions that the suites of MMgr link, from
  # MMgr/test/support/host_wait.c. That file sets its own feature set before its first include.
  add_library(pc_mmgr_host_hooks OBJECT "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/test/support/host_wait.c")
  target_include_directories(pc_mmgr_host_hooks PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/test/support"
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded_types/include")
  if(WIN32)
    # WaitOnAddress and WakeByAddressAll are in the synchronization library.
    target_link_libraries(pc_mmgr_host_hooks PUBLIC synchronization)
  endif()
endif()

# idemIP is a separate repository and a submodule here. This file does not enter the build
# description of idemIP, because that description fetches embedded_types, reads MMgr from its own
# submodule and builds its suites. ProtoCore builds the idemIP sources as one more target, against
# the MMgr and embedded_types above. The program thus has one copy of each.
#
# The source lists are the ones that idemIP's CMakeLists.txt selects, capability for capability. All
# five capabilities are on, and include/idemip_capabilities.h gives each IDEMIP_ENABLE_* value. The
# include root is include/idemIP, since the tree includes itself as "src/...".
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/idemIP/src")
  set(_pc_idemip_src "${CMAKE_CURRENT_SOURCE_DIR}/idemIP/src")
  add_library(pc_idemip OBJECT
    "${_pc_idemip_src}/common.c"
    "${_pc_idemip_src}/checksum.c"
    "${_pc_idemip_src}/ms.c"
    "${_pc_idemip_src}/clock.c"
    "${_pc_idemip_src}/core/dispatch.c"
    "${_pc_idemip_src}/core/stats.c"
    "${_pc_idemip_src}/core/tick.c"
    "${_pc_idemip_src}/core/timeouts.c"
    "${_pc_idemip_src}/ethernet/ethernet.c"
    "${_pc_idemip_src}/ethernet/mii.c"
    "${_pc_idemip_src}/ethernet/phy.c"
    "${_pc_idemip_src}/ethernet/vlan.c"
    "${_pc_idemip_src}/netif/dma.c"
    "${_pc_idemip_src}/netif/loopif.c"
    "${_pc_idemip_src}/netif/netif.c"
    "${_pc_idemip_src}/acd/acd.c"
    "${_pc_idemip_src}/arp/arp.c"
    "${_pc_idemip_src}/arp/arp_table.c"
    "${_pc_idemip_src}/autoip/autoip.c"
    "${_pc_idemip_src}/dhcp/dhcp4.c"
    "${_pc_idemip_src}/icmp/icmp.c"
    "${_pc_idemip_src}/icmp/icmp_in.c"
    "${_pc_idemip_src}/igmp/igmp.c"
    "${_pc_idemip_src}/ip/ipv4.c"
    "${_pc_idemip_src}/ip/ip4_addr.c"
    "${_pc_idemip_src}/ip/ip4_forward.c"
    "${_pc_idemip_src}/ip/ip4_frag.c"
    "${_pc_idemip_src}/ip/ip4_reass.c"
    "${_pc_idemip_src}/ip/ip4_route.c"
    "${_pc_idemip_src}/pmtu/pmtu4.c"
    "${_pc_idemip_src}/dhcp/dhcp6.c"
    "${_pc_idemip_src}/ethernet/ethip6.c"
    "${_pc_idemip_src}/icmp/icmpv6.c"
    "${_pc_idemip_src}/icmp/icmp6_in.c"
    "${_pc_idemip_src}/ip/ipv6.c"
    "${_pc_idemip_src}/ip/ip6_addr.c"
    "${_pc_idemip_src}/ip/ip6_forward.c"
    "${_pc_idemip_src}/ip/ip6_frag.c"
    "${_pc_idemip_src}/ip/ip6_reass.c"
    "${_pc_idemip_src}/ip/ip6_select.c"
    "${_pc_idemip_src}/mld/mld6.c"
    "${_pc_idemip_src}/nd/dad.c"
    "${_pc_idemip_src}/nd/nd6.c"
    "${_pc_idemip_src}/nd/rdnss.c"
    "${_pc_idemip_src}/nd/slaac.c"
    "${_pc_idemip_src}/pmtu/pmtu6.c"
    "${_pc_idemip_src}/tcp/tcp.c"
    "${_pc_idemip_src}/tcp/tcp_in.c"
    "${_pc_idemip_src}/tcp/tcp_isn.c"
    "${_pc_idemip_src}/tcp/tcp_out.c"
    "${_pc_idemip_src}/tcp/tcp_pcb.c"
    "${_pc_idemip_src}/dns/dns.c"
    "${_pc_idemip_src}/udp/udp.c"
    "${_pc_idemip_src}/udp/udp_pcb.c"
    "${_pc_idemip_src}/udp/udplite.c"
    "${_pc_idemip_src}/raw/raw_pcb.c"
    "${_pc_idemip_src}/ip/pseudo.c"
    "${_pc_idemip_src}/ip/pseudo_v4.c"
    "${_pc_idemip_src}/ip/pseudo_v6.c")
  # This directory holds idemip_capabilities.h, which idemip_config.h reads from the include path. That
  # header reads the board profile, under the repository root, which comes after the root of idemIP.
  target_include_directories(pc_idemip PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/idemIP"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/MMgr/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded_types/include")
  if(MINGW)
    target_compile_options(pc_idemip PRIVATE -ffunction-sections -fdata-sections)
  endif()
endif()
