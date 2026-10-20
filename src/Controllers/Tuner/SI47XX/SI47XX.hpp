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

#ifndef FMDX_TUNER_SI47XX_H
#define FMDX_TUNER_SI47XX_H

#include <Arduino.h>
#include "../TunerDriver.hpp"
#include "../../../I2c/I2cDriver.hpp"
#include "../../../../Config.hpp"
#include "../../../../ConfigSI47XX.hpp"

class SI47XX : public TunerDriver
{
public:
    void setup() override;
    bool start() override;
    void shutdown() override;
    void hello() override;
    void loop() override;

    void resetQuality() override;
    void resetRds() override;

    bool setMode(Mode value) override;
    bool setFrequency(uint32_t value, TuneFlags flags) override;
    bool setBandwidth(uint32_t value) override;
    bool setDeemphasis(uint32_t value) override;
    bool setAgc(uint32_t value) override;
    bool setAlignment(uint32_t value) override;
    bool setVolume(uint8_t value) override;
    bool setOutputMode(OutputMode value) override;
    bool setCustom(const char *name, const char *value) override;

    bool getQuality() override;
    int16_t getQualityRssi(QualityMode mode) override;
    int16_t getQualityCci(QualityMode mode) override;
    int16_t getQualityAci(QualityMode mode) override;
    int16_t getQualityModulation(QualityMode mode) override;
    int16_t getQualityOffset(QualityMode mode) override;
    int16_t getQualityBandwidth(QualityMode mode) override;
    bool getQualityStereo(QualityMode mode) override;

    const char *getName() override;

private:
    bool detectI2cAddress();
    bool powerUp(Mode value);
    bool powerDown();

    bool sendCommand(const uint8_t *data, size_t len, bool expectResponse = false);
    bool readResponse(uint8_t *data, size_t len);
    bool waitCts(uint16_t timeoutMs);
    bool setProperty(uint16_t prop, uint16_t value);
    bool applyBlend();

    bool readRsq();
    bool readTuneStatus();
    bool readRds();

    I2cDriver i2cLow{0x22};
    I2cDriver i2cHigh{0xC6};
    I2cDriver *i2c = &i2cHigh;
    uint8_t i2cAddr7 = 0x63;
    char tuner[16] = "SI47XX";

    bool devicePresent = false;
    bool qualityValid = false;
    int16_t lastRssi = 0;
    bool lastStereo = false;
    int16_t lastSnr = 0;
    int16_t lastFreqOffset = 0;
    uint32_t lastQualityMs = 0;
    uint32_t lastRdsMs = 0;
    uint32_t lastTuneMs = 0;
    bool scanTune = false;
    bool forceMono = false;
    uint16_t lastAntcap = 0;
    uint16_t scanAntcap = 0;
};

#endif
