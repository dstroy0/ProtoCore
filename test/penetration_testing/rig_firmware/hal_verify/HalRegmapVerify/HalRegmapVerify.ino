// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Ground-truth cross-check for the self-contained RSA/MPI HAL (src/core_setup/hal/esp/esp_crypto_hal.h): every PC_
// register value the HAL hardcodes is static_assert'd EQUAL to the manufacturer's own soc macro for the target die. The
// HAL deliberately includes no soc/ header; THIS test does (it is the only place the two meet), so a mismatch - a typo
// in our map, or an upstream register change - is a compile error. Compile it per FQBN (verify_regmaps.sh) for every
// supported die; a clean compile proves that die's map matches Espressif's TRM implementation, even for dies we have no
// board for. Boards we DO have are additionally KAT'd on-device (main_cryptobench). esp_crypto_hal.h is staged next to
// this sketch by verify_regmaps.sh - the HAL is self-contained, so this needs no library attach (and thus no WiFi,
// which the H2 lacks).
#include "esp_crypto_hal.h"

#ifndef PC_RSA_MODMUL_HW
#error "HalRegmapVerify: PC_RSA_MODMUL_HW not set for this die - it has no HAL map to check"
#endif

#include "soc/hwcrypto_reg.h" // manufacturer RSA register map (RSA_*)

// ---- RSA block registers (offsets identical across generations; names differ ver1 vs ver3) ----
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32C3)
// hw_ver1 names
static_assert(PC_RSA_MEM_M == RSA_MEM_M_BLOCK_BASE, "RSA M mem");
static_assert(PC_RSA_MEM_X == RSA_MEM_X_BLOCK_BASE, "RSA X mem");
static_assert(PC_RSA_MEM_Y == RSA_MEM_Y_BLOCK_BASE, "RSA Y mem");
static_assert(PC_RSA_MEM_Z == RSA_MEM_Z_BLOCK_BASE, "RSA Z mem");
static_assert(PC_RSA_MPRIME == RSA_M_DASH_REG, "RSA m'");
static_assert(PC_RSA_MODE == RSA_LENGTH_REG, "RSA mode/length");
static_assert(PC_RSA_START == RSA_MOD_MULT_START_REG, "RSA start");
static_assert(PC_RSA_DONE == RSA_QUERY_INTERRUPT_REG, "RSA done");
static_assert(PC_RSA_INTCLR == RSA_CLEAR_INTERRUPT_REG, "RSA int clr");
static_assert(PC_RSA_INTENA == RSA_INTERRUPT_REG, "RSA int ena");
#else
// hw_ver3 names (P4 / C6 / C5 / H2). H2's headers suffix the memory blocks with _REG; the rest are shared.
#if defined(CONFIG_IDF_TARGET_ESP32H2)
static_assert(PC_RSA_MEM_M == RSA_M_MEM_REG, "RSA M mem");
static_assert(PC_RSA_MEM_X == RSA_X_MEM_REG, "RSA X mem");
static_assert(PC_RSA_MEM_Y == RSA_Y_MEM_REG, "RSA Y mem");
static_assert(PC_RSA_MEM_Z == RSA_Z_MEM_REG, "RSA Z mem");
#else
static_assert(PC_RSA_MEM_M == RSA_M_MEM, "RSA M mem");
static_assert(PC_RSA_MEM_X == RSA_X_MEM, "RSA X mem");
static_assert(PC_RSA_MEM_Y == RSA_Y_MEM, "RSA Y mem");
static_assert(PC_RSA_MEM_Z == RSA_Z_MEM, "RSA Z mem");
#endif
static_assert(PC_RSA_MPRIME == RSA_M_PRIME_REG, "RSA m'");
static_assert(PC_RSA_MODE == RSA_MODE_REG, "RSA mode");
static_assert(PC_RSA_START == RSA_SET_START_MODMULT_REG, "RSA start");
static_assert(PC_RSA_DONE == RSA_QUERY_IDLE_REG, "RSA done/idle");
static_assert(PC_RSA_INTCLR == RSA_INT_CLR_REG, "RSA int clr");
static_assert(PC_RSA_INTENA == RSA_INT_ENA_REG, "RSA int ena");
#endif
static_assert(PC_RSA_CLEAN == RSA_QUERY_CLEAN_REG, "RSA clean");

// ---- Clock / reset / power (per-die subsystem: SYSTEM / DPORT / HP_SYS_CLKRST / PCR) ----
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
#include "soc/system_reg.h"
static_assert(PC_RSA_CLK_REG == SYSTEM_PERIP_CLK_EN1_REG, "clk reg");
static_assert(PC_RSA_CLK_BIT == SYSTEM_CRYPTO_RSA_CLK_EN, "clk bit");
static_assert(PC_RSA_RST_REG == SYSTEM_PERIP_RST_EN1_REG, "rst reg");
static_assert(PC_RSA_RST_BIT == SYSTEM_CRYPTO_RSA_RST, "rst bit");
static_assert(PC_RSA_HOLD_REG == SYSTEM_PERIP_RST_EN1_REG, "hold reg");
static_assert(PC_RSA_HOLD_CLEAR == SYSTEM_CRYPTO_DS_RST, "hold bit");
static_assert(PC_RSA_PD_REG == SYSTEM_RSA_PD_CTRL_REG, "pd reg");
static_assert(PC_RSA_PD_UP_CLEAR == SYSTEM_RSA_MEM_PD, "pd bit");
static_assert(PC_RSA_PD_DOWN_BIT == SYSTEM_RSA_MEM_PD, "pd status bit");
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#include "soc/dport_reg.h"
static_assert(PC_RSA_CLK_REG == DPORT_PERIP_CLK_EN1_REG, "clk reg");
static_assert(PC_RSA_CLK_BIT == DPORT_CRYPTO_RSA_CLK_EN, "clk bit");
static_assert(PC_RSA_RST_REG == DPORT_PERIP_RST_EN1_REG, "rst reg");
static_assert(PC_RSA_RST_BIT == DPORT_CRYPTO_RSA_RST, "rst bit");
static_assert(PC_RSA_HOLD_REG == DPORT_PERIP_RST_EN1_REG, "hold reg");
static_assert(PC_RSA_HOLD_CLEAR == DPORT_CRYPTO_DS_RST, "hold bit");
static_assert(PC_RSA_PD_REG == DPORT_RSA_PD_CTRL_REG, "pd reg");
static_assert(PC_RSA_PD_UP_CLEAR == DPORT_RSA_MEM_PD, "pd bit");
static_assert(PC_RSA_PD_DOWN_BIT == DPORT_RSA_MEM_PD, "pd status bit");
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
#include "soc/hp_sys_clkrst_reg.h"
static_assert(PC_RSA_CLK_REG == HP_SYS_CLKRST_PERI_CLK_CTRL25_REG, "clk reg");
static_assert(PC_RSA_CLK_BIT == HP_SYS_CLKRST_REG_CRYPTO_RSA_CLK_EN, "clk bit");
static_assert(PC_RSA_RST_REG == HP_SYS_CLKRST_HP_RST_EN2_REG, "rst reg");
static_assert(PC_RSA_RST_BIT == HP_SYS_CLKRST_REG_RST_EN_RSA, "rst bit");
static_assert(PC_RSA_HOLD_REG == HP_SYS_CLKRST_HP_RST_EN2_REG, "hold reg");
static_assert(PC_RSA_HOLD_CLEAR ==
                  (HP_SYS_CLKRST_REG_RST_EN_CRYPTO | HP_SYS_CLKRST_REG_RST_EN_DS | HP_SYS_CLKRST_REG_RST_EN_ECDSA),
              "hold bits");
#else // C6 / C5 / H2 - PCR
#include "soc/pcr_reg.h"
static_assert(PC_RSA_CLK_REG == PCR_RSA_CONF_REG, "clk reg");
static_assert(PC_RSA_CLK_BIT == PCR_RSA_CLK_EN, "clk bit");
static_assert(PC_RSA_RST_REG == PCR_RSA_CONF_REG, "rst reg");
static_assert(PC_RSA_RST_BIT == PCR_RSA_RST_EN, "rst bit");
static_assert(PC_RSA_HOLD_REG == PCR_DS_CONF_REG, "hold reg");
static_assert(PC_RSA_HOLD_CLEAR == PCR_DS_RST_EN, "hold bit");
static_assert(PC_RSA_PD_REG == PCR_RSA_PD_CTRL_REG, "pd reg");
static_assert(PC_RSA_PD_UP_CLEAR == (PCR_RSA_MEM_PD | PCR_RSA_MEM_FORCE_PD), "pd bits");
static_assert(PC_RSA_PD_DOWN_BIT == PCR_RSA_MEM_PD, "pd status bit");
#if defined(CONFIG_IDF_TARGET_ESP32C5) || defined(CONFIG_IDF_TARGET_ESP32H2)
static_assert(PC_RSA_HOLD2_REG == PCR_ECDSA_CONF_REG, "hold2 reg");
static_assert(PC_RSA_HOLD2_CLEAR == PCR_ECDSA_RST_EN, "hold2 bit");
#endif
#endif

void setup()
{
}
void loop()
{
}
