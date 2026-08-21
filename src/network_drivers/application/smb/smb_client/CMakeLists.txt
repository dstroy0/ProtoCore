# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/application/smb/smb_client
  GATE PROTOCORE_ENABLE_SMB
  SOURCES smb_client.c
  DEPS
    network_drivers/application/smb/smb2
  PRIVATE_DEPS
    crypto/rng
    mmgr/protomem
    mmgr/secure
    network_drivers/application/smb/ntlm
    network_drivers/application/smb/ntlmssp
    network_drivers/application/smb/spnego
)
