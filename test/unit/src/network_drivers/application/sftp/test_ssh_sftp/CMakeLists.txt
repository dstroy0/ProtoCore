# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_ssh_sftp: test_ssh_sftp.c, plus the generated Unity runner.

protocore_add_suite(unit/src/network_drivers/application/sftp/test_ssh_sftp
  SOURCES
    test_ssh_sftp.c
)
