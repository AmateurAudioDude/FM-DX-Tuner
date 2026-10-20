/*  SPDX-License-Identifier: GPL-3.0-or-later
 *
 *  FM-DX Tuner SI47XX Driver
 *  Copyright (c) 2026 Mark de Bruijn
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 3
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef FMDX_TUNER_CONFIG_SI47XX_H
#define FMDX_TUNER_CONFIG_SI47XX_H

/* -----------------------------------------------------------------------
   SI47XX configuration (SI4730/31/34/35 family)
   This code is partially based on or influenced by the following documents/code:
   - Skyworks AN332 (SI47xx Programming Guide)
   - Si4730/31/34/35-D60 datasheet
   - https://github.com/pu2clr/SI4735
   - https://github.com/esp32-si4732/ats-mini

   Board selection
   ----------------------------------------------------------------------- */
#define SI47XX_BOARD_ATS_MINI 1
#define SI47XX_BOARD_ATS_20 2

/* Default board (available: SI47XX_BOARD_ATS_MINI, SI47XX_BOARD_ATS_20) */
/* ATS-20 support is present but not yet confirmed working */
#define SI47XX_BOARD SI47XX_BOARD_ATS_MINI

/* -----------------------------------------------------------------------
   Board-specific settings
   ----------------------------------------------------------------------- */
#if SI47XX_BOARD == SI47XX_BOARD_ATS_MINI
/* I2C address: use 8-bit write address.
   Possible: 0x22 (7-bit 0x11, SEN low) or 0xC6 (7-bit 0x63, SEN high).
   7-bit equivalents shown for reference. */
#define SI47XX_I2C_ADDRESS 0xC6

/* Defaults (ATS-Mini):
   - I2C address: 0xC6 (7-bit 0x63)
   - I2C pins: SDA=18, SCL=17
   - I2C clock: 800 kHz
   - Reset: GPIO16
   - Power-up delay: 500 ms
   - CTS timeout: 150 ms
   - STC timeout/poll: 250 ms / 1 ms */
/* I2C pins (ESP32). Set to -1 to use Wire defaults. */
#define SI47XX_I2C_SDA 18
#define SI47XX_I2C_SCL 17
#define SI47XX_I2C_CLOCK 800000UL
#define SI47XX_POST_RESET_DELAY_MS 0
#define SI47XX_POWERUP_DELAY_MS 500
#define SI47XX_TUNE_DELAY_MS 0
#define SI47XX_CTS_TIMEOUT_MS 150
#define SI47XX_STC_TIMEOUT_MS 250
#define SI47XX_STC_POLL_MS 1

/* Radio power enable pin (active high). Set to -1 to disable. */
#define SI47XX_RADIO_EN_PIN 15

/* Audio amp enable (active high). Set to -1 to disable. */
#define SI47XX_AMP_EN_PIN 10

/* Audio mute (active high). Set to -1 to disable. */
#define SI47XX_MUTE_PIN 3

/* Optional reset pin. Set to -1 to disable reset toggling. */
#define SI47XX_RESET_PIN 16

/* Power-up options */
#define SI47XX_XOSCEN 1    /* 0 = external RCLK, 1 = crystal */
#define SI47XX_OPMODE 0x05 /* 0x05 = analog audio outputs */
#define SI47XX_AVR_PULLUP false
#elif SI47XX_BOARD == SI47XX_BOARD_ATS_20
/* ATS-20 (Arduino Nano) */
/* Possible: 0x22 (7-bit 0x11, SEN low) or 0xC6 (7-bit 0x63, SEN high).
   7-bit equivalents shown for reference. */
#define SI47XX_I2C_ADDRESS 0xC6

/* Defaults (ATS-20):
   - I2C address: 0xC6 (7-bit 0x63)
   - I2C pins: default Nano SDA/SCL (A4/A5)
   - I2C clock: 100 kHz
   - Reset: D12
   - Post-reset delay: 20 ms
   - Power-up delay: 600 ms
   - CTS timeout: 300 ms
   - STC timeout/poll: 500 ms / 20 ms */
/* I2C pins (ESP32 only). Use defaults on AVR/Nano. */
#define SI47XX_I2C_SDA -1
#define SI47XX_I2C_SCL -1
#define SI47XX_I2C_CLOCK 100000UL
#define SI47XX_POST_RESET_DELAY_MS 20
#define SI47XX_POWERUP_DELAY_MS 600
#define SI47XX_TUNE_DELAY_MS 0
#define SI47XX_CTS_TIMEOUT_MS 300
#define SI47XX_STC_TIMEOUT_MS 500
#define SI47XX_STC_POLL_MS 20

/* No external power/amp/mute control on ATS-20 board */
#define SI47XX_RADIO_EN_PIN -1
#define SI47XX_AMP_EN_PIN -1
#define SI47XX_MUTE_PIN -1

/* Reset pin from ATS-20 sketch */
#define SI47XX_RESET_PIN 12

/* Power-up options */
#define SI47XX_XOSCEN 1    /* uses crystal */
#define SI47XX_OPMODE 0x05 /* 0x05 = analog audio outputs */

/* AVR internal I2C pull-ups (Nano only; use true if no external pull-ups) */
#define SI47XX_AVR_PULLUP false
#else
#error "Unknown SI47XX_BOARD selection"
#endif

/* Reference clock (set to 0 to skip configuration) */
#define SI47XX_REFCLK_FREQ 0
#define SI47XX_REFCLK_PRESCALE 1

/* Feature flags */
#define SI47XX_HAS_RDS 1

/* Default bands (kHz) */
#define SI47XX_FM_BAND_MIN 64000
#define SI47XX_FM_BAND_MAX 108000
#define SI47XX_AM_BAND_MIN 149
#define SI47XX_AM_BAND_MAX 23000

/* Default start frequencies (kHz) */
#define SI47XX_DEFAULT_FM_FREQ 105900
#define SI47XX_DEFAULT_AM_FREQ 1000

/* Driver timing */
#define SI47XX_QUALITY_INTERVAL_MS 60
#define SI47XX_RDS_INTERVAL_MS 90
#define SI47XX_SCAN_SETTLE_MS 5
#define SI47XX_SCAN_SETTLE_AM_MS 5

/* FM scan uses the chip's fast tune (1 = enabled, 0 = disabled) */
#define SI47XX_SCAN_FAST_TUNE 1

/* AM scan uses the chip's fast tune (1 = enabled, 0 = disabled) */
#define SI47XX_SCAN_FAST_TUNE_AM 1

/* AM scan reuses the antenna capacitor found on its first point (1 = enabled, 0 = disabled) */
#define SI47XX_SCAN_FIXED_ANTCAP_AM 1

/* AM tunes skip the valid station check (1 = enabled, 0 = disabled) */
#define SI47XX_AM_SKIP_VALID_CHECK 1

/* ----------------------------------------------------------------------- */

#endif
