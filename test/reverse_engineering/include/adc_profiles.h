// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file adc_profiles.h
 * @brief Reference constants for the ADC_DMA front end's candidate ADCs - resolution,
 *        pipeline latency, and whether the part has a software-configurable SPI port.
 *
 * Any sub-GSPS ADC works behind the FPGA/CPLD burst-capture front end described in
 * main.cpp's top comment (mmgr/dma drains a triggered burst; server/signaling/trace_capture
 * holds an arbitrary pretrigger ring and flushes the whole pre+post window on trigger) -
 * this file is just the handful of per-part numbers that differ, so swapping the target
 * ADC is a `-DDAQ_ADC_PROFILE=...` build flag, not a rewrite:
 *
 *  - **AD9226** (12-bit, up to 65 MSPS, single channel): pure parallel CMOS output, no SPI
 *    port at all - "does not need to be configured by software" per the datasheet. Nothing
 *    to write to it; the only number that matters here is its pipeline latency.
 *  - **AD9238** (12-bit, 20/40/65 MSPS, dual channel): the same parallel-bus family, PLUS a
 *    real SPI configuration port (power-down, output format, test patterns - see
 *    services/peripherals/ad9238/ad9238.h). main.cpp's ad9238_bringup() only runs when the selected
 *    profile has one.
 *  - **DAQ_ADC_GENERIC**: a conservative stand-in for any other sub-GSPS part behind the
 *    same FPGA/CPLD burst-drain architecture (TI ADSxxxx, other ADI AD92xx/AD98xx siblings)
 *    - fill in your part's real numbers rather than shipping traces with the wrong scaling.
 *  - **DAQ_ADC_INTERNAL_AUDIO**: no external ADC chip at all - the ESP32's own built-in ADC,
 *    polled via the standard Arduino `analogRead()`, feeding a $1-2 piezo disc or electret
 *    microphone. Audio-band acoustic leakage (coil whine, capacitor/relay clicks) is real,
 *    famous, and entirely within this profile's reach - see Genkin, Shamir & Tromer, "RSA Key
 *    Extraction via Low-Bandwidth Acoustic Cryptanalysis" (CRYPTO 2014). This is a genuinely
 *    different ingestion CODE PATH (main.cpp's internal-ADC polling task, not mmgr/dma at
 *    all - there is no external peripheral to drive), selected by DAQ_ADC_INTERNAL_POLLED.
 *
 * **Pipeline latency.** Every pipelined ADC reports sample N for an analog instant that was
 * actually N - latency_samples clock cycles earlier (the datasheet states this in ADC clock
 * cycles from encode to output valid). trace_capture's pc_tc_trigger() freezes the
 * pre-trigger ring at the instant the external trigger GPIO fires, but the samples already
 * *in* that ring at that instant lag the analog signal by the ADC's pipeline latency -
 * DAQ_ADC_PIPELINE_LATENCY_SAMPLES documents that offset so analysis can shift the reported
 * trigger index back onto the true analog trigger instant instead of silently misaligning it.
 */

#ifndef REVERSE_ENGINEERING_ADC_PROFILES_H
#define REVERSE_ENGINEERING_ADC_PROFILES_H

#define DAQ_ADC_AD9226 1         ///< 12-bit, 65 MSPS max, single channel, no SPI port
#define DAQ_ADC_AD9238 2         ///< 12-bit, 65 MSPS max, dual channel, has an SPI config port
#define DAQ_ADC_GENERIC 3        ///< fill in DAQ_ADC_* below for your specific part
#define DAQ_ADC_INTERNAL_AUDIO 4 ///< ESP32's own built-in ADC, polled - no external chip needed

#ifndef DAQ_ADC_PROFILE
#define DAQ_ADC_PROFILE DAQ_ADC_AD9238
#endif

#if DAQ_ADC_PROFILE == DAQ_ADC_AD9226
#define DAQ_ADC_RESOLUTION_BITS 12
#define DAQ_ADC_MAX_SAMPLE_RATE_HZ 65000000.0f
#define DAQ_ADC_PIPELINE_LATENCY_SAMPLES 7 // AD9226 datasheet: 7 encode-clock cycles, encode-to-data-valid
#define DAQ_ADC_HAS_SPI_CONFIG 0
#define DAQ_ADC_CHANNEL_COUNT 1

#elif DAQ_ADC_PROFILE == DAQ_ADC_AD9238
#define DAQ_ADC_RESOLUTION_BITS 12
#define DAQ_ADC_MAX_SAMPLE_RATE_HZ 65000000.0f
#define DAQ_ADC_PIPELINE_LATENCY_SAMPLES 9 // AD9238 datasheet: 9 encode-clock cycles, encode-to-data-valid
#define DAQ_ADC_HAS_SPI_CONFIG 1
#define DAQ_ADC_CHANNEL_COUNT 2 // dual - main.cpp's single-channel wiring uses channel A only by default

#elif DAQ_ADC_PROFILE == DAQ_ADC_INTERNAL_AUDIO
#define DAQ_ADC_RESOLUTION_BITS 12 // ESP32 ADC1/ADC2 native resolution
#define DAQ_ADC_MAX_SAMPLE_RATE_HZ                                                                                     \
    100000.0f                              // a conservative ceiling for a plain analogRead() polling
                                           // loop; ample headroom over audio-band (~20 kHz) leakage
#define DAQ_ADC_PIPELINE_LATENCY_SAMPLES 0 // a single-shot SAR conversion per analogRead() call - no pipeline
#define DAQ_ADC_HAS_SPI_CONFIG 0
#define DAQ_ADC_CHANNEL_COUNT 1

#else // DAQ_ADC_GENERIC - a conservative placeholder; confirm every value against your part's datasheet
#define DAQ_ADC_RESOLUTION_BITS 12
#define DAQ_ADC_MAX_SAMPLE_RATE_HZ 10000000.0f
#define DAQ_ADC_PIPELINE_LATENCY_SAMPLES 0
#define DAQ_ADC_HAS_SPI_CONFIG 0
#define DAQ_ADC_CHANNEL_COUNT 1
#endif

/** @brief Selects main.cpp's ingestion code path: 1 = a simple analogRead() polling task (no
 *         mmgr/dma, no external peripheral); 0 = mmgr/dma + trace_capture as usual. */
#if DAQ_ADC_PROFILE == DAQ_ADC_INTERNAL_AUDIO
#define DAQ_ADC_INTERNAL_POLLED 1
#else
#define DAQ_ADC_INTERNAL_POLLED 0
#endif

#define DAQ_ADC_FULL_SCALE_CODES ((uint32_t)1 << DAQ_ADC_RESOLUTION_BITS)

#endif // REVERSE_ENGINEERING_ADC_PROFILES_H
