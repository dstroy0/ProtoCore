# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Real-protocol interop peers, one module per protocol family.

Each module exposes either module-level NAME / HELP / add_args(parser) / run(args),
or a PEERS list of peer objects with the same four attributes (for families that
ship both a client and a server peer, e.g. Modbus and OPC UA). The interop CLI in
interop.py turns each into an argparse subcommand.
"""
