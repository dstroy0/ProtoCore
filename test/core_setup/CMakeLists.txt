# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# THE PLATFORM ARM. src/ declares seams - a platform header, a crypto HAL, an NVS interface - and
# names no answer to them. This tree holds the answers a build that is not a device uses, and the
# esp arm holds the ones a device uses.
#
# It is under test/ because the host answers were written for the suites, but src/ depends on it in
# a plain build too: pc_config carries `test/core_setup/hal/host` on its include path for every
# module, and three modules include `nvs.h`, which exists only at test/core_setup/hal/nvs.h. So the
# seam header for a src/ module lives in this tree - stated here because it is easy to read the
# directory name and assume otherwise.
#
# WHY THE ARMS ARE NOT GATED. host, portable and esp answer the same seams three ways, and which
# one a build takes is the consumer's statement - the matrix names the arm's sources per env, 204
# envs taking portable_platform.c and 10 taking host_platform.c. A GATE is for a module that is not
# in the build at all; an alternative is not that. The esp arm is the exception below, and for a
# different reason: it cannot compile without the ESP-IDF headers.

add_subdirectory(boot)
add_subdirectory(hal)
add_subdirectory(idemIP)
