# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/codec
  HEADER_ONLY
  DEPS
    mmgr/span
)

add_subdirectory(base64)
add_subdirectory(cbor)
add_subdirectory(deflate)
add_subdirectory(hpack_prim)
add_subdirectory(inflate)
add_subdirectory(json)
add_subdirectory(msgpack)
add_subdirectory(multipart)

