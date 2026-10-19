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

#include <Arduino.h>
#include <Wire.h>
#include "../../../Comm.hpp"
#ifdef ARDUINO_ARCH_ESP32
#include <driver/gpio.h>
#endif
#include "SI47XX.hpp"

namespace
{
    constexpr uint8_t CMD_POWER_UP = 0x01;
    constexpr uint8_t CMD_POWER_DOWN = 0x11;
    constexpr uint8_t CMD_SET_PROPERTY = 0x12;
    constexpr uint8_t CMD_GET_INT_STATUS = 0x14;

    constexpr uint8_t CMD_FM_TUNE_STATUS = 0x22;
    constexpr uint8_t CMD_FM_TUNE_FREQ = 0x20;
    constexpr uint8_t CMD_FM_RSQ_STATUS = 0x23;
    constexpr uint8_t CMD_FM_RDS_STATUS = 0x24;
    constexpr uint8_t CMD_FM_AGC_OVERRIDE = 0x28;

    constexpr uint8_t CMD_AM_TUNE_STATUS = 0x42;
    constexpr uint8_t CMD_AM_TUNE_FREQ = 0x40;
    constexpr uint8_t CMD_AM_RSQ_STATUS = 0x43;
    constexpr uint8_t CMD_AM_AGC_OVERRIDE = 0x48;

    constexpr uint16_t PROP_FM_DEEMPHASIS = 0x1100;
    constexpr uint16_t PROP_FM_CHANNEL_FILTER = 0x1102;
    constexpr uint16_t PROP_FM_BLEND_MONO_THRESHOLD = 0x1106;
    constexpr uint16_t PROP_FM_BLEND_STEREO_THRESHOLD = 0x1105;
    constexpr uint16_t PROP_FM_SEEK_TUNE_RSSI_THRESHOLD = 0x1404;
    constexpr uint16_t PROP_FM_RDS_CONFIG = 0x1502;

    constexpr uint16_t PROP_AM_CHANNEL_FILTER = 0x3102;
    constexpr uint16_t PROP_RX_VOLUME = 0x4000;
    constexpr uint16_t PROP_RX_HARD_MUTE = 0x4001;

    constexpr uint16_t PROP_REFCLK_FREQ = 0x0201;
    constexpr uint16_t PROP_REFCLK_PRESCALE = 0x0202;
}

void
SI47XX::setup()
{
#if SI47XX_AMP_EN_PIN >= 0
    pinMode(SI47XX_AMP_EN_PIN, OUTPUT);
    digitalWrite(SI47XX_AMP_EN_PIN, LOW);
#endif

#if SI47XX_MUTE_PIN >= 0
    pinMode(SI47XX_MUTE_PIN, OUTPUT);
    digitalWrite(SI47XX_MUTE_PIN, HIGH);
#endif

#if SI47XX_RADIO_EN_PIN >= 0
    pinMode(SI47XX_RADIO_EN_PIN, OUTPUT);
    digitalWrite(SI47XX_RADIO_EN_PIN, HIGH);
    delay(100);
#endif

#if defined(ARDUINO_ARCH_ESP32)
#if defined(SI47XX_I2C_SDA) && defined(SI47XX_I2C_SCL)
    if (SI47XX_I2C_SDA >= 0 && SI47XX_I2C_SCL >= 0)
    {
        Wire.begin(SI47XX_I2C_SDA, SI47XX_I2C_SCL);
        Wire.setClock(SI47XX_I2C_CLOCK);
        gpio_set_drive_capability((gpio_num_t)SI47XX_I2C_SDA, GPIO_DRIVE_CAP_0);
        gpio_set_drive_capability((gpio_num_t)SI47XX_I2C_SCL, GPIO_DRIVE_CAP_0);
    }
    else
    {
        this->i2c->init();
    }
#else
    this->i2c->init();
#endif
#else
    Wire.begin();
    Wire.setClock(SI47XX_I2C_CLOCK);
    this->i2c->init();
#endif

#if SI47XX_RESET_PIN >= 0
    pinMode(SI47XX_RESET_PIN, OUTPUT);
    digitalWrite(SI47XX_RESET_PIN, LOW);
    delay(5);
    digitalWrite(SI47XX_RESET_PIN, HIGH);
    delay(5);
#if SI47XX_POST_RESET_DELAY_MS > 0
    delay(SI47XX_POST_RESET_DELAY_MS);
#endif
#endif

    this->devicePresent = this->detectI2cAddress();
}

bool
SI47XX::start()
{
    if (!this->devicePresent)
    {
        return false;
    }

 #if SI47XX_MUTE_PIN >= 0
    digitalWrite(SI47XX_MUTE_PIN, HIGH);
 #endif

 #if SI47XX_RESET_PIN >= 0
    digitalWrite(SI47XX_RESET_PIN, LOW);
    delay(10);
    digitalWrite(SI47XX_RESET_PIN, HIGH);
    delay(10);
#if SI47XX_POST_RESET_DELAY_MS > 0
    delay(SI47XX_POST_RESET_DELAY_MS);
#endif
 #endif

    if (this->mode == MODE_NONE)
    {
        this->mode = MODE_FM;
        this->frequency = SI47XX_DEFAULT_FM_FREQ;
        this->step = 10;
    }

    if (!this->powerUp(this->mode))
    {
        return false;
    }
    delay(250);

    if (!this->setFrequency(this->frequency, TUNE_DEFAULT))
    {
        return false;
    }

    if (this->mode == MODE_FM)
    {
        // Seek validity is unused, an unreachable threshold may shorten tunes on occupied channels
        this->setProperty(PROP_FM_SEEK_TUNE_RSSI_THRESHOLD, 127);
        this->setProperty(PROP_FM_RDS_CONFIG, 0xAA01);
        this->resetRds();
    }

    delay(50);
 #if SI47XX_MUTE_PIN >= 0
    digitalWrite(SI47XX_MUTE_PIN, LOW);
 #endif
 #if SI47XX_AMP_EN_PIN >= 0
    digitalWrite(SI47XX_AMP_EN_PIN, HIGH);
 #endif

    this->qualityValid = false;
    this->lastQualityMs = 0;
    this->lastRdsMs = 0;
    return true;
}

void
SI47XX::shutdown()
{
    if (this->devicePresent)
    {
        this->powerDown();
    }
    this->qualityValid = false;
}

void
SI47XX::hello()
{
}

void
SI47XX::loop()
{
    if (this->mode != MODE_FM && this->mode != MODE_AM)
    {
        return;
    }

    const uint32_t now = millis();

    // A scan needs one fresh reading per point, not the listening hold-off or RDS
    if (this->scanTune)
    {
        if (!this->qualityValid &&
            (now - this->lastTuneMs) >= SI47XX_SCAN_SETTLE_MS &&
            (now - this->lastQualityMs) >= SI47XX_SCAN_SETTLE_MS)
        {
            this->readRsq();
            this->lastQualityMs = now;
        }
        return;
    }

    if ((now - this->lastTuneMs) < 500)
    {
        return;
    }

    if ((now - this->lastQualityMs) >= SI47XX_QUALITY_INTERVAL_MS)
    {
        this->readRsq();
        this->lastQualityMs = now;
    }

#if SI47XX_HAS_RDS
    if (this->mode == MODE_FM &&
        (now - this->lastRdsMs) >= SI47XX_RDS_INTERVAL_MS)
    {
        this->readRds();
        this->lastRdsMs = now;
    }
#endif
}

void
SI47XX::resetQuality()
{
    this->qualityValid = false;
}

void
SI47XX::resetRds()
{
    this->piBuffer.clear();
    this->rdsBuffer.clear();

#if SI47XX_HAS_RDS
    if (this->mode == MODE_FM)
    {
        const uint8_t cmd[] = { CMD_FM_RDS_STATUS, 0x02 };
        this->sendCommand(cmd, sizeof(cmd), true);
        uint8_t resp[13];
        this->readResponse(resp, sizeof(resp));
    }
#endif
}

bool
SI47XX::setMode(Mode value)
{
    if (value != MODE_FM && value != MODE_AM)
    {
        return false;
    }

    if (this->mode == value)
    {
        return true;
    }

    if (!this->powerDown())
    {
        return false;
    }

    this->mode = value;

    if (this->mode == MODE_FM)
    {
        this->step = 10;
        if (this->frequency < SI47XX_FM_BAND_MIN ||
            this->frequency > SI47XX_FM_BAND_MAX)
        {
            this->frequency = SI47XX_DEFAULT_FM_FREQ;
        }
    }
    else
    {
        this->step = 1;
        if (this->frequency < SI47XX_AM_BAND_MIN ||
            this->frequency > SI47XX_AM_BAND_MAX)
        {
            this->frequency = SI47XX_DEFAULT_AM_FREQ;
        }
    }

    if (!this->powerUp(this->mode))
    {
        return false;
    }

    if (!this->setFrequency(this->frequency, TUNE_DEFAULT))
    {
        return false;
    }

    if (this->mode == MODE_FM)
    {
        this->setProperty(PROP_FM_SEEK_TUNE_RSSI_THRESHOLD, 127);
        this->setProperty(PROP_FM_RDS_CONFIG, 0xAA01);
        this->resetRds();
    }

    return true;
}

bool
SI47XX::setFrequency(uint32_t value,
                     TuneFlags flags)
{
    if (value >= SI47XX_FM_BAND_MIN &&
        value <= SI47XX_FM_BAND_MAX)
    {
        if (this->mode != MODE_FM)
        {
            if (!this->setMode(MODE_FM))
            {
                return false;
            }
        }

        const uint16_t freq10khz = (uint16_t)(value / 10);
        const uint8_t fast = (SI47XX_SCAN_FAST_TUNE && (flags & TUNE_SCAN)) ? 0x01 : 0x00;
        const uint8_t freeze = 0x00;
        const uint8_t cmd[] =
        {
            CMD_FM_TUNE_FREQ,
            (uint8_t)(freeze | fast),
            (uint8_t)(freq10khz >> 8),
            (uint8_t)(freq10khz & 0xFF),
            0x00
        };

        if (!this->sendCommand(cmd, sizeof(cmd)))
        {
            return false;
        }

        this->lastTuneMs = millis();
        this->scanTune = (flags & TUNE_SCAN) != 0;
        delay(SI47XX_TUNE_DELAY_MS);
        this->qualityValid = false;
        // A fast tune's status may be inaccurate, so report the frequency actually tuned
        if (!this->readTuneStatus() || fast)
        {
            this->frequency = (uint32_t)freq10khz * 10U;
        }
        return true;
    }

    if (value >= SI47XX_AM_BAND_MIN &&
        value <= SI47XX_AM_BAND_MAX)
    {
        if (this->mode != MODE_AM)
        {
            if (!this->setMode(MODE_AM))
            {
                return false;
            }
        }

        const uint16_t freqkhz = (uint16_t)value;
        const uint8_t fast = 0x00;
        const uint8_t cmd[] =
        {
            CMD_AM_TUNE_FREQ,
            fast,
            (uint8_t)(freqkhz >> 8),
            (uint8_t)(freqkhz & 0xFF),
            0x00,
            0x00
        };

        if (!this->sendCommand(cmd, sizeof(cmd)))
        {
            return false;
        }

        this->lastTuneMs = millis();
        this->scanTune = (flags & TUNE_SCAN) != 0;
        delay(SI47XX_TUNE_DELAY_MS);
        this->qualityValid = false;
        if (!this->readTuneStatus())
        {
            this->frequency = value;
        }
        return true;
    }

    return false;
}

bool
SI47XX::setBandwidth(uint32_t value)
{
    if (this->mode == MODE_FM)
    {
        uint16_t filter = 0;
        uint32_t actual = 0;

        if (value == 0)
        {
            filter = 0;
            actual = 0;
        }
        else if (value >= 100000)
        {
            filter = 1;
            actual = 110000;
        }
        else if (value >= 80000)
        {
            filter = 2;
            actual = 84000;
        }
        else if (value >= 60000)
        {
            filter = 3;
            actual = 60000;
        }
        else
        {
            filter = 4;
            actual = 40000;
        }

        if (!this->setProperty(PROP_FM_CHANNEL_FILTER, filter))
        {
            return false;
        }

        this->bandwidth = actual;
        return true;
    }

    if (this->mode == MODE_AM)
    {
        uint16_t filter = 3;
        uint32_t actual = 2000;

        switch (value)
        {
            case 6000: filter = 0; actual = 6000; break;
            case 4000: filter = 1; actual = 4000; break;
            case 3000: filter = 2; actual = 3000; break;
            case 2000: filter = 3; actual = 2000; break;
            case 1000: filter = 4; actual = 1000; break;
            case 1800: filter = 5; actual = 1800; break;
            case 2500: filter = 6; actual = 2500; break;
            default:
                if (value == 0)
                {
                    filter = 3;
                    actual = 2000;
                }
                break;
        }

        if (!this->setProperty(PROP_AM_CHANNEL_FILTER, filter))
        {
            return false;
        }

        this->bandwidth = actual;
        return true;
    }

    return false;
}

bool
SI47XX::setDeemphasis(uint32_t value)
{
    if (this->mode != MODE_FM)
    {
        return false;
    }

    uint16_t deemph;
    if (value == 50 || value == 0)
    {
        deemph = 0x0001;
    }
    else if (value == 75)
    {
        deemph = 0x0002;
    }
    else
    {
        return false;
    }

    return this->setProperty(PROP_FM_DEEMPHASIS, deemph);
}

bool
SI47XX::setAgc(uint32_t value)
{
    const uint32_t clamped = constrain(value, 0U, 3U);
    const uint8_t agcDisabled = (clamped == 0) ? 0 : 1; /* 0 = auto AGC, 1 = manual attenuation */
    const uint8_t agcIdx = (clamped == 0) ? 0 : (uint8_t)map(clamped, 1U, 3U, 0U, 36U);

    const uint8_t cmd = (this->mode == MODE_AM) ? CMD_AM_AGC_OVERRIDE : CMD_FM_AGC_OVERRIDE;
    const uint8_t data[] = { cmd, agcDisabled, agcIdx };
    return this->sendCommand(data, sizeof(data));
}

bool
SI47XX::setAlignment(uint32_t value)
{
    this->alignment = value;
    return true;
}

bool
SI47XX::setVolume(uint8_t value)
{
    if (value > 100)
    {
        return false;
    }

    const uint16_t vol = (uint16_t)map(value, 0U, 100U, 0U, 63U);
    if (!this->setProperty(PROP_RX_VOLUME, vol))
    {
        return false;
    }
    return this->setProperty(PROP_RX_HARD_MUTE, 0);
}

bool
SI47XX::setOutputMode(OutputMode value)
{
    if (this->mode != MODE_FM)
    {
        return false;
    }

    if (value == OUTPUT_MODE_MONO)
    {
        if (!this->setProperty(PROP_FM_BLEND_MONO_THRESHOLD, 0x007F))
        {
            return false;
        }
        return this->setProperty(PROP_FM_BLEND_STEREO_THRESHOLD, 0x007F);
    }

    if (value == OUTPUT_MODE_STEREO)
    {
        if (!this->setProperty(PROP_FM_BLEND_MONO_THRESHOLD, 0x0000))
        {
            return false;
        }
        return this->setProperty(PROP_FM_BLEND_STEREO_THRESHOLD, 0x0000);
    }

    return false;
}

bool
SI47XX::setCustom(const char *name,
                  const char *value)
{
    (void)name;
    (void)value;
    return true;
}

bool
SI47XX::getQuality()
{
    return this->qualityValid;
}

int16_t
SI47XX::getQualityRssi(QualityMode mode)
{
    (void)mode;
    if (!this->qualityValid)
    {
        return 0;
    }

    const int16_t rssi = this->lastRssi * 100;
    const int16_t dbfOffset = 1125;
    return rssi + dbfOffset;
}

int16_t
SI47XX::getQualityCci(QualityMode mode)
{
    (void)mode;
    return 0;
}

int16_t
SI47XX::getQualityAci(QualityMode mode)
{
    (void)mode;
    return 0;
}

int16_t
SI47XX::getQualityModulation(QualityMode mode)
{
    (void)mode;
    return -1;
}

int16_t
SI47XX::getQualityOffset(QualityMode mode)
{
    (void)mode;
    return this->lastFreqOffset;
}

int16_t
SI47XX::getQualityBandwidth(QualityMode mode)
{
    (void)mode;
    return -1;
}

bool
SI47XX::getQualityStereo(QualityMode mode)
{
    (void)mode;
    return this->lastStereo;
}

const char*
SI47XX::getName()
{
    return this->tuner;
}

bool
SI47XX::powerUp(Mode value)
{
    const uint8_t func = (value == MODE_AM) ? 0x01 : 0x00;
    const uint8_t arg1 = (uint8_t)(
        (func & 0x0F) |
        ((SI47XX_XOSCEN & 0x01) << 4));

    const uint8_t arg2 = (uint8_t)SI47XX_OPMODE;

    const uint8_t cmd[] = { CMD_POWER_UP, arg1, arg2 };
    if (!this->sendCommand(cmd, sizeof(cmd)))
    {
        return false;
    }

    if (SI47XX_XOSCEN)
    {
        delay(SI47XX_POWERUP_DELAY_MS);
    }

    if (SI47XX_XOSCEN == 0 && SI47XX_REFCLK_FREQ != 0)
    {
        if (!this->setProperty(PROP_REFCLK_FREQ, (uint16_t)SI47XX_REFCLK_FREQ))
        {
            return false;
        }
        if (!this->setProperty(PROP_REFCLK_PRESCALE, (uint16_t)SI47XX_REFCLK_PRESCALE))
        {
            return false;
        }
    }

    return true;
}

bool
SI47XX::powerDown()
{
    const uint8_t cmd[] = { CMD_POWER_DOWN };
    return this->sendCommand(cmd, sizeof(cmd));
}

bool
SI47XX::detectI2cAddress()
{
    uint8_t address7 = 0x63;
#if defined(SI47XX_I2C_ADDRESS)
    if (SI47XX_I2C_ADDRESS == 0x22)
    {
        address7 = 0x11;
    }
    else if (SI47XX_I2C_ADDRESS == 0xC6)
    {
        address7 = 0x63;
    }
#endif

    Wire.beginTransmission(address7);
    if (Wire.endTransmission() != 0)
    {
        return false;
    }

    if (address7 == 0x63)
    {
        this->i2c = &this->i2cHigh;
        this->i2cAddr7 = 0x63;
    }
    else
    {
        this->i2c = &this->i2cLow;
        this->i2cAddr7 = 0x11;
    }
    return true;
}
bool
SI47XX::sendCommand(const uint8_t *data, size_t len, bool expectResponse)
{
    if (!this->waitCts(SI47XX_CTS_TIMEOUT_MS))
    {
        return false;
    }

    Wire.beginTransmission(this->i2cAddr7);
    for (size_t i = 0; i < len; i++)
    {
        if (Wire.write(data[i]) != 1)
        {
            Wire.endTransmission();
            return false;
        }
    }
    if (Wire.endTransmission() != 0)
    {
        return false;
    }

    if (expectResponse)
    {
        return true;
    }

    return this->waitCts(SI47XX_CTS_TIMEOUT_MS);
}

bool
SI47XX::readResponse(uint8_t *data, size_t len)
{
    if (!this->waitCts(SI47XX_CTS_TIMEOUT_MS))
    {
        return false;
    }

    const uint8_t readCount = Wire.requestFrom(this->i2cAddr7, (uint8_t)len, (uint8_t)true);
    if (readCount != len)
    {
        return false;
    }
    for (size_t i = 0; i < len; i++)
    {
        data[i] = (uint8_t)Wire.read();
    }
    return true;
}

bool
SI47XX::waitCts(uint16_t timeoutMs)
{
    const uint32_t start = millis();
    while ((millis() - start) < timeoutMs)
    {
        const uint8_t readCount = Wire.requestFrom(this->i2cAddr7, (uint8_t)1, (uint8_t)true);
        if (readCount != 1)
        {
            delayMicroseconds(200);
            continue;
        }
        const uint8_t status = (uint8_t)Wire.read();

        if (status & 0x80)
        {
            return true;
        }

        delayMicroseconds(200);
    }
    return false;
}

bool
SI47XX::setProperty(uint16_t prop, uint16_t value)
{
    const uint8_t cmd[] =
    {
        CMD_SET_PROPERTY,
        0x00,
        (uint8_t)(prop >> 8),
        (uint8_t)(prop & 0xFF),
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF)
    };
    return this->sendCommand(cmd, sizeof(cmd));
}

bool
SI47XX::readRsq()
{
    const bool fm = (this->mode == MODE_FM);
    const uint8_t cmd[] = { (uint8_t)(fm ? CMD_FM_RSQ_STATUS : CMD_AM_RSQ_STATUS), 0x00 };
    if (!this->sendCommand(cmd, sizeof(cmd), true))
    {
        return false;
    }

    const uint8_t respLen = fm ? 8 : 6;
    uint8_t resp[8] = {0};
    if (!this->readResponse(resp, respLen))
    {
        return false;
    }

    this->lastRssi = resp[4];
    this->lastSnr = resp[5];
    this->lastStereo = fm ? ((resp[3] & 0x80) != 0) : false;
    this->lastFreqOffset = fm ? (int8_t)resp[7] : 0;
    this->qualityValid = true;
    return true;
}

bool
SI47XX::readTuneStatus()
{
    const bool fm = (this->mode == MODE_FM);
    const uint8_t cmd = (uint8_t)(fm ? CMD_FM_TUNE_STATUS : CMD_AM_TUNE_STATUS);
    uint8_t resp[8] = {0};
    bool stc = false;

    const uint32_t start = millis();
    while ((millis() - start) < SI47XX_STC_TIMEOUT_MS)
    {
        const uint8_t query[] = { cmd, 0x00 };
        if (!this->sendCommand(query, sizeof(query), true))
        {
            return false;
        }
        if (!this->readResponse(resp, sizeof(resp)))
        {
            return false;
        }

        stc = (resp[0] & 0x01) != 0;
        if (stc)
        {
            break;
        }

        delay(SI47XX_STC_POLL_MS);
    }

    if (!stc)
    {
        return false;
    }

    const uint16_t readFreq = (uint16_t)((resp[2] << 8) | resp[3]);
    this->frequency = fm ? ((uint32_t)readFreq * 10U) : (uint32_t)readFreq;
    this->lastRssi = resp[4];
    this->lastSnr = resp[5];
    this->lastFreqOffset = 0;
    this->qualityValid = true;

    const uint8_t clear[] = { cmd, 0x01 };
    this->sendCommand(clear, sizeof(clear), true);
    this->readResponse(resp, sizeof(resp));
    return true;
}

bool
SI47XX::readRds()
{
#if SI47XX_HAS_RDS
    if (this->mode != MODE_FM)
    {
        return false;
    }

    const uint8_t cmd[] = { CMD_FM_RDS_STATUS, 0x00 };
    if (!this->sendCommand(cmd, sizeof(cmd), true))
    {
        return false;
    }

    uint8_t resp[13] = {0};
    if (!this->readResponse(resp, sizeof(resp)))
    {
        return false;
    }

    const uint8_t rdsFlags = resp[1];
    const bool newBlockA = (rdsFlags & 0x10) != 0;
    const bool newBlockB = (rdsFlags & 0x20) != 0;
    if (!newBlockA && !newBlockB)
    {
        return true;
    }

    const uint16_t blockA = (uint16_t)resp[4] << 8 | resp[5];
    const uint16_t blockB = (uint16_t)resp[6] << 8 | resp[7];
    const uint16_t blockC = (uint16_t)resp[8] << 8 | resp[9];
    const uint16_t blockD = (uint16_t)resp[10] << 8 | resp[11];
    const uint8_t ble = resp[12];

    const uint8_t bleA = (ble >> 6) & 0x03;
    const uint8_t bleB = (ble >> 4) & 0x03;
    const uint8_t bleC = (ble >> 2) & 0x03;
    const uint8_t bleD = (ble >> 0) & 0x03;

    this->piBuffer.add(blockA, bleA != 0);
    this->rdsBuffer.set(RdsGroupBuffer::BLOCK_A, blockA, bleA);
    this->rdsBuffer.set(RdsGroupBuffer::BLOCK_B, blockB, bleB);
    this->rdsBuffer.set(RdsGroupBuffer::BLOCK_C, blockC, bleC);
    this->rdsBuffer.set(RdsGroupBuffer::BLOCK_D, blockD, bleD);
    return true;
#else
    return false;
#endif
}
