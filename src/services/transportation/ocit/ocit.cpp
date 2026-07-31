// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ocit.cpp
 * @brief OCIT-Outstations message codec (see ocit.h).
 */

#include "services/transportation/ocit/ocit.h"

#if PC_ENABLE_OCIT

#include <string.h>

size_t pc_ocit_build(uint8_t msg_type, uint16_t object_type, uint16_t instance, uint8_t data_type, const uint8_t *value,
                     size_t value_len, uint8_t *out, size_t cap)
{
    if (!out || (value_len && !value))
    {
        return 0;
    }
    size_t n = 6 + value_len; // msg-type + object-type(2) + instance(2) + data-type + value
    if (n > cap)
    {
        return 0;
    }
    out[0] = msg_type;
    out[1] = (uint8_t)(object_type >> 8);
    out[2] = (uint8_t)object_type;
    out[3] = (uint8_t)(instance >> 8);
    out[4] = (uint8_t)instance;
    out[5] = data_type;
    if (value_len)
    {
        memcpy(out + 6, value, value_len);
    }
    return n;
}

size_t pc_ocit_set_u16(uint16_t object_type, uint16_t instance, uint16_t value, uint8_t *out, size_t cap)
{
    uint8_t v[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return pc_ocit_build(OcitMsgType::OCIT_MSG_SET, object_type, instance, OcitType::OCIT_TYPE_UINT16, v, 2, out, cap);
}

bool pc_ocit_parse(const uint8_t *msg, size_t len, OcitMsg *out)
{
    if (!msg || !out || len < 6)
    {
        return false;
    }
    out->msg_type = msg[0];
    out->object_type = (uint16_t)((msg[1] << 8) | msg[2]);
    out->instance = (uint16_t)((msg[3] << 8) | msg[4]);
    out->data_type = msg[5];
    out->value = (len > 6) ? (msg + 6) : nullptr;
    out->value_len = len - 6;
    return true;
}

uint16_t pc_ocit_value_u16(const OcitMsg *m)
{
    if (!m || m->data_type != OcitType::OCIT_TYPE_UINT16 || m->value_len < 2 || !m->value)
    {
        return 0;
    }
    return (uint16_t)((m->value[0] << 8) | m->value[1]);
}

#endif // PC_ENABLE_OCIT
