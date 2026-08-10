// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_mnt_fs.h
 * @brief Mount backend over an Arduino `fs::FS` (LittleFS / SD / SPIFFS).
 *
 * This is the only file in the library that names a vendor filesystem type, and it is in the board
 * layer for that reason: the core describes storage as a vtable of plain C function pointers
 * (services/storage/mnt) and never sees the framework behind it. An application mounts this
 * backend once at startup and everything above it - SFTP, SCP, WebDAV, file serving - reaches it
 * through the filesystem accessor without knowing the medium.
 *
 * Example: `pc_mnt_mount(pc_mnt_fs(&LittleFS));`
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ESP_MNT_FS_H
#define PROTOCORE_ESP_MNT_FS_H

#include "protocore_config.h"

#if PC_ENABLE_MNT && defined(ARDUINO)

#include "server/filesystem/mnt.h"

namespace fs
{
class FS;
}

/**
 * @brief Bind @p filesystem and return the backend to hand to pc_mnt_mount().
 *
 * @param filesystem an already-mounted Arduino filesystem (call its own begin() first).
 * @return the backend vtable; it stays valid for the life of the program.
 */
const pc_mnt_backend *pc_mnt_fs(fs::FS *filesystem);

#endif // PC_ENABLE_MNT && ARDUINO
#endif // PROTOCORE_ESP_MNT_FS_H
