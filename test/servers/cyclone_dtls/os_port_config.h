// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Cyclone OS-port config for the DTLS 1.3 interop client (test tooling; see run_interop.sh).
#ifndef _OS_PORT_CONFIG_H
#define _OS_PORT_CONFIG_H

// Accept the GPL terms so CycloneCRYPTO/SSL compile (see tls.h / crypto.h).
#define GPL_LICENSE_TERMS_ACCEPTED

// Host build: no RTOS, POSIX port is auto-selected on __linux__ by os_port.h.
#define USE_POSIX

#endif
