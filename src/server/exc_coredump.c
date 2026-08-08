// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file exc_coredump.c
 * @brief Read the ESP32 core-dump partition and offload it (see exc_decoder.h).
 *
 * The panic text a device prints dies with the reboot; the core dump ESP-IDF writes to flash does
 * not. This reads that image back on the next boot - as a summary for the live panel, and as raw
 * bytes for a durable copy - then erases it so the next crash gets the space.
 *
 * Device-only: the summary structs and partition API are ESP-IDF's. The decoding/serialization of a
 * panic stays pure in exc_decoder.c.
 */

#include "server/exc_decoder.h"

#if PC_ENABLE_EXC_DECODER && PROTOCORE_HOT

#include <esp_core_dump.h>
#include <esp_partition.h>

proto_bool pc_exc_coredump_present(ExcCoreDump *out)
{
    size_t addr = 0;
    size_t size = 0;
    // image_get reports what is stored; image_check verifies its checksum, so a torn write from a
    // crash mid-dump is reported as absent rather than handed on as a valid image.
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0)
    {
        return PROTO_FALSE;
    }
    if (esp_core_dump_image_check() != ESP_OK)
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        out->addr = (uint32_t)addr;
        out->size = size;
    }
    return PROTO_TRUE;
}

proto_bool pc_exc_coredump_summary(ExcInfo *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    esp_core_dump_summary_t s;
    memset(&s, 0, sizeof(s));
    if (esp_core_dump_get_summary(&s) != ESP_OK)
    {
        return PROTO_FALSE;
    }

    memset(out, 0, sizeof(*out));
    out->core = -1; // the summary does not name the core; the raw image does
    out->pc = s.exc_pc;
    // The crashing task name is the most useful short label the summary carries, so it fills the
    // slot the console decoder puts the Guru Meditation cause in.
    strncpy(out->cause, s.exc_task, sizeof(out->cause) - 1);
    out->cause[sizeof(out->cause) - 1] = '\0';

#if CONFIG_IDF_TARGET_ARCH_XTENSA
    // Xtensa's windowed ABI lets the device walk its own stack, so a real backtrace is stored.
    out->excvaddr = s.ex_info.exc_vaddr;
    out->has_excvaddr = PROTO_TRUE;
    size_t depth = s.exc_bt_info.depth;
    if (depth > PC_EXC_MAX_FRAMES)
    {
        depth = PC_EXC_MAX_FRAMES;
    }
    for (size_t i = 0; i < depth; i++)
    {
        out->frames[i].pc = s.exc_bt_info.bt[i];
        out->frames[i].sp = 0; // not part of the summary
    }
    out->frame_count = depth;
#else
    // RISC-V records a stack dump, not a backtrace - unwinding it needs DWARF, which lives off
    // device. Report the trap cause/value and leave frame_count 0 rather than inventing frames.
    out->excvaddr = s.ex_info.mtval;
    out->has_excvaddr = PROTO_TRUE;
    out->frame_count = 0;
#endif
    return PROTO_TRUE;
#else
    (void)out;
    return PROTO_FALSE; // built without flash/ELF core dumps
#endif
}

// Locate the image inside its partition. image_get reports an absolute flash address, but
// esp_partition_read wants an offset within the partition, so every reader converts here once.
static proto_bool coredump_locate(const esp_partition_t **part_out, size_t *base_out, size_t *size_out)
{
    ExcCoreDump img;
    if (!pc_exc_coredump_present(&img))
    {
        return PROTO_FALSE;
    }
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (!part || img.addr < part->address)
    {
        return PROTO_FALSE;
    }
    size_t base = (size_t)(img.addr - part->address);
    if (base + img.size > part->size)
    {
        return PROTO_FALSE;
    }
    *part_out = part;
    *base_out = base;
    *size_out = img.size;
    return PROTO_TRUE;
}

proto_bool pc_exc_coredump_read(size_t offset, void *buf, size_t len)
{
    if (!buf)
    {
        return PROTO_FALSE;
    }
    if (len == 0)
    {
        return PROTO_TRUE;
    }

    const esp_partition_t *part = NULL;
    size_t base = 0;
    size_t size = 0;
    if (!coredump_locate(&part, &base, &size))
    {
        return PROTO_FALSE;
    }
    // Refuse a range that runs past the image rather than returning whatever flash follows it.
    if (offset > size || len > size - offset)
    {
        return PROTO_FALSE;
    }
    return esp_partition_read(part, base + offset, buf, len) == ESP_OK;
}

proto_bool pc_exc_coredump_save(const pc_mnt_backend *file_sys, const char *path)
{
    if (!file_sys || !path || path[0] == '\0')
    {
        return PROTO_FALSE;
    }

    const esp_partition_t *part = NULL;
    size_t base = 0;
    size_t size = 0;
    if (!coredump_locate(&part, &base, &size))
    {
        return PROTO_FALSE;
    }

    int fh = file_sys->open(path, PC_MNT_WRITE);
    if (fh < 0)
    {
        return PROTO_FALSE;
    }

    // Streamed in fixed chunks: a dump can be tens of KB and must never need to fit RAM at once.
    uint8_t buf[PC_EXC_COREDUMP_CHUNK];
    size_t off = 0;
    proto_bool ok = PROTO_TRUE;
    while (off < size)
    {
        size_t n = (size - off < sizeof(buf)) ? size - off : sizeof(buf);
        if (esp_partition_read(part, base + off, buf, n) != ESP_OK || file_sys->write(fh, buf, n) != (int)n)
        {
            ok = PROTO_FALSE;
            break;
        }
        off += n;
    }
    file_sys->close(fh);
    if (!ok)
    {
        (void)file_sys->remove(path); // never leave a half-written dump that looks complete
    }
    return ok;
}

proto_bool pc_exc_coredump_erase(void)
{
    return esp_core_dump_image_erase() == ESP_OK;
}

#endif // PC_ENABLE_EXC_DECODER && PROTOCORE_HOT
