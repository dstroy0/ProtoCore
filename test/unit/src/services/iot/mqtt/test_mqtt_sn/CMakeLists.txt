# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_mqtt_sn: test_mqtt_sn.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/iot/mqtt/test_mqtt_sn
  SOURCES
    test_mqtt_sn.c
)
