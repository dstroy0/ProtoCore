// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Test tooling only, NOT part of the library; builds against Oryx CycloneSSH/CycloneCRYPTO
// (GPLv2), fetched at run time by run_interop.sh - never vendored here.
#ifndef _OS_PORT_CONFIG_H
#define _OS_PORT_CONFIG_H

// Accept the GPL terms so CycloneCRYPTO/CycloneSSH compile (see ssh.h / crypto.h).
#define GPL_LICENSE_TERMS_ACCEPTED

// Host build: no RTOS. os_port.h auto-selects the POSIX port on __linux__.
#define USE_POSIX

#endif
