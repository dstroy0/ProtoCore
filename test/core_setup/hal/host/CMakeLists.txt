# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The host arm: the seams answered in software on the machine that runs the suite.
#
# Two kinds of file live here and both belong to this one module. The .c files are the answers -
# a platform clock and entropy, an NVS over a file, a DMA that is a memcpy, and the crypto HALs.
# The headers with no .c beside them are mocks standing in for something the host does not have:
# Arduino.h, WiFi.h, FS.h, the lwip and freertos trees, the I2C device models, lfs_mock.h for the
# filesystem and mnt_mock.h for the mount.
#
# THE CRYPTO HALs ARE THE HARDWARE ARM, ANSWERED IN SOFTWARE. host_sha_hal.c and host_aes_hal.c
# exist so an env can set PROTOCORE_HAS_HW_SHA and take the hardware path on a machine that has no
# accelerator - which is how native_sha256_kat_hw tests the same module as native_sha256_kat down
# the other branch. They are not a second implementation of SHA; they are the seam.
#
# pc_config already carries this directory on every module's include path, so a src/ module reaching
# a mock here resolves today whether or not it links this target. That is the include path doing it,
# not a declared dependency - which is the reason to declare one.

protocore_add_module(test/core_setup/hal/host
  SOURCES
    host_aes_hal.c
    host_bignum.c
    host_crypto_hal.c
    host_hw_reg.c
    host_nvs.c
    host_platform.c
    host_sha_hal.c
    protocore_dma_host.c
  DEPS
    test/core_setup/hal
    crypto/asymmetric/bignum
    crypto/cipher/aes_block
    mmgr/protomem
    mmgr/secure
    server/storage/mnt
)

add_subdirectory(physical)
