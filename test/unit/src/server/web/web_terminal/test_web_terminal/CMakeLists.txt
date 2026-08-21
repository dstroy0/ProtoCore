# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_web_terminal: test_web_terminal.c.
# Writes its own main(), so it takes no generated Unity runner.

protocore_add_suite(unit/src/server/web/web_terminal/test_web_terminal
  OWN_MAIN
  SOURCES
    test_web_terminal.c
)
