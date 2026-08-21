# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The directories below. A new module is a new directory and a line here - nothing central holds a
# list of modules, so there is nothing else to keep in step.

add_subdirectory(h3_conn)
add_subdirectory(h3_frame)
add_subdirectory(h3_server)
add_subdirectory(qpack)
add_subdirectory(quic_conn)
add_subdirectory(quic_crypto)
add_subdirectory(quic_frame)
add_subdirectory(quic_packet)
add_subdirectory(quic_server)
add_subdirectory(quic_tls)
add_subdirectory(quic_tp)
add_subdirectory(quic_varint)
add_subdirectory(tls13_msg)
add_subdirectory(tls13_rpk)
