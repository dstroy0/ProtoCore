# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Makes test/ a package so its scripts run as `python -m test.<name>`, which puts the repo root on
# sys.path and lets them import tools.findroot. Run as a bare path, sys.path[0] is test/ and that
# import cannot resolve.
