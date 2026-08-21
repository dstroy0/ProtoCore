# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_ftp_session: test_ftp_session.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/file_transfer/ftp/test_ftp_session
  SOURCES
    test_ftp_session.c
)
