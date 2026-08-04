// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ota_service.h
 * @brief Optional authenticated OTA firmware update (PC_ENABLE_OTA).
 *
 * Registers a POST endpoint that streams a firmware image straight into the
 * ESP32 `Update` API via the parser's streaming-body hook
 * (http_parser_set_stream_hooks), so the image never has to fit in RAM. On a
 * successful flash the device responds and reboots into the new firmware.
 * Compiled to a no-op stub when PC_ENABLE_OTA is 0 or off-Arduino.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_OTA_SERVICE_H
#define PROTOCORE_OTA_SERVICE_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_OTA

/**
 * @brief Register an authenticated streaming OTA endpoint.
 *
 * Call after begin(). A `POST @p path` carrying a raw firmware image and valid
 * HTTP Basic credentials is streamed into `Update`; on success the device
 * replies `200` and reboots. Unauthorized or failed uploads get `401` / `400`.
 *
 * @param server  The running server (the route + stream hooks are installed on it).
 * @param path    URL to accept the upload on (e.g. "/update"). Persistent string.
 * @param user    Required HTTP Basic username.
 * @param pass    Required HTTP Basic password.
 *
 * @code
 * curl -u admin:s3cret --data-binary @firmware.bin http://<ip>/update
 * @endcode
 */
void pc_ota_begin(const char *path, const char *user, const char *pass);

#endif // PC_ENABLE_OTA

PROTO_END_DECLS

#endif // PROTOCORE_OTA_SERVICE_H
