# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The directories below. A new suite is a new directory and a line here - nothing central holds a
# list of suites, so there is nothing else to keep in step.

add_subdirectory(h3_conn)
add_subdirectory(h3_server)
add_subdirectory(quic_conn)
add_subdirectory(quic_server)
add_subdirectory(test_dtls_tls13)
add_subdirectory(test_h3_frame)
add_subdirectory(test_qpack)
add_subdirectory(test_quic_crypto)
add_subdirectory(test_quic_frame)
add_subdirectory(test_quic_packet)
add_subdirectory(test_quic_tls)
add_subdirectory(test_quic_tp)
add_subdirectory(test_quic_varint)
add_subdirectory(test_tls13_msg)
add_subdirectory(test_tls13_rpk)
