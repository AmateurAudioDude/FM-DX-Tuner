/*  SPDX-License-Identifier: GPL-3.0-or-later
 *
 *  FM-DX Tuner
 *  Copyright (C) 2023-2025  Konrad Kosmatka
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
#include "../../Comm.hpp"
#include "Tuner.hpp"
#include "../../Utils/Utils.hpp"
#include "../../Protocol.h"
#include "../../../Config.hpp"
#include "../Led/Led.hpp"

void
Tuner::setup()
{
    this->driver.setup();
}

void
Tuner::hello()
{
    this->driver.hello();
}

void
Tuner::loop()
{
    this->driver.loop();

    if (this->scan.isActive())
    {
        this->scan.process();
        return;
    }

    this->handleRds();

    if (this->driver.getQuality())
    {
        if (timerQuality.process(Timer::Continous))
        {
            this->handleQuality();
        }
    }
    
    if (timerSquelch.process(Timer::Continous))
    {
        this->handleSquelch();
        this->handleAutosquelch();
    }
}

const Command*
Tuner::getCommands(uint8_t *len)
{
    static constexpr Command commands[] =
    {
        { FMDX_TUNER_PROTOCOL_CANCEL, &this->cbCancel },
        { FMDX_TUNER_PROTOCOL_MODE, &this->cbMode },
        { FMDX_TUNER_PROTOCOL_TUNE, &this->cbFrequency },
        { FMDX_TUNER_PROTOCOL_DEEMPHASIS, &this->cbDeemphasis },
        { FMDX_TUNER_PROTOCOL_AGC, &this->cbAgc },
        { FMDX_TUNER_PROTOCOL_ALIGNMENT, &this->cbAlignment },
        { FMDX_TUNER_PROTOCOL_BANDWIDTH, &this->cbBandwidth },
        { FMDX_TUNER_PROTOCOL_VOLUME, &this->cbVolume },
        { FMDX_TUNER_PROTOCOL_SQUELCH, &this->cbSquelch },
        { FMDX_TUNER_PROTOCOL_AUTOSQUELCH, &this->cbAutosquelch },
        { FMDX_TUNER_PROTOCOL_OUTPUT_MODE, &this->cbOutputMode },
        { FMDX_TUNER_PROTOCOL_QUALITY, &this->cbQuality },
        { FMDX_TUNER_PROTOCOL_SCAN, &this->cbScan },
        { FMDX_TUNER_PROTOCOL_CUSTOM, &this->cbCustom },
        { FMDX_TUNER_PROTOCOL_SEEK, &this->cbSeek }
    };

    *len = sizeof(commands) / sizeof(Command);
    return commands;
}

bool
Tuner::start()
{
    if (this->driver.start())
    {
        constexpr Timer::Interval qualityInterval = 66;
        this->timerQuality.set(qualityInterval);

        constexpr Timer::Interval squelchInterval = 50;
        this->timerSquelch.set(squelchInterval);

#if LED_ENABLED && defined(LED_ID_POWER)
        led.on(LED_ID_POWER);
#endif

        return true;
    }

    return false;
}

void
Tuner::shutdown()
{
    this->driver.shutdown();

    this->timerQuality.unset();
    this->timerSquelch.unset();

#ifdef ARDUINO_ARCH_AVR
    /* Release SDA and SCL lines used by hardware I2C */
    TWCR = 0;
#endif

#if LED_ENABLED && defined(LED_ID_POWER)
    led.off(LED_ID_POWER);
#endif
}

void
Tuner::clear()
{
    this->driver.resetQuality();
    this->driver.resetRds();
    this->driver.piBuffer.clear();
    this->driver.rdsBuffer.clear();
}

void
Tuner::handleQuality()
{
    const auto pilot = this->driver.getQualityStereo(this->qualityMode);
    const auto rssi = this->driver.getQualityRssi(this->qualityMode);
    const auto cci = this->driver.getQualityCci(this->qualityMode);
    const auto aci = this->driver.getQualityAci(this->qualityMode);
    const auto bw = this->driver.getQualityBandwidth(this->qualityMode);
    const auto usn = this->driver.getQualityNoise(this->qualityMode);
    const auto wam = this->driver.getQualityCoChannel(this->qualityMode);

    Comm.print('S');

    if (this->stereo)
    {
        Comm.print(pilot ? 's' : 'm');
    }
    else
    {
        Comm.print(pilot ? 'S' : 'M');
    }

    Utils::serialDecimal(rssi, 2);

    Comm.print(',');
    Comm.print(cci, DEC);
    Comm.print(',');
    Comm.print(aci, DEC);
    Comm.print(',');
    Comm.print(bw, DEC);
    Comm.print(',');
    Comm.print(usn, DEC);
    Comm.print(',');
    Comm.print(wam, DEC);
    Comm.print('\n');
}

void
Tuner::handleRds()
{
    uint16_t pi;
    RdsPiBuffer::State piState = this->driver.piBuffer.get(pi);
    if (piState != RdsPiBuffer::STATE_INVALID)
    {
        Comm.print('P');
        Utils::serialHexPi(pi, (uint8_t)piState);
        Comm.print('\n');
    }

    uint16_t *rdsData;
    uint8_t rdsStatus;
    if (this->driver.rdsBuffer.get(&rdsData, &rdsStatus))
    {
        Comm.print('R');
#if TUNER_LEGACY_RDS_MSG == false
        Utils::serialHex16(rdsData[0]);
#endif
        Utils::serialHex16(rdsData[1]);
        Utils::serialHex16(rdsData[2]);
        Utils::serialHex16(rdsData[3]);
#if TUNER_LEGACY_RDS_MSG == false
        Utils::serialHex8(rdsStatus);
#else
        uint8_t legacyStatus = 0;
        legacyStatus |= (rdsStatus & B00110000) >> 4;
        legacyStatus |= (rdsStatus & B00001100);
        legacyStatus |= (rdsStatus & B00000011) << 4;
        Utils::serialHex8(legacyStatus);
#endif
        Comm.print('\n');
    }
}

void
Tuner::handleSquelch()
{
    constexpr TunerDriver::QualityMode qualityMode = TunerDriver::QUALITY_DEFAULT;

    int8_t value;
    switch (this->squelch.getMode())
    {
        case SquelchMode::SQUELCH_RSSI:
            value = (int8_t)(this->driver.getQualityRssi(qualityMode) / 100);
            break;

        case SquelchMode::SQUELCH_STEREO:
            value = (int8_t)this->driver.getQualityStereo(qualityMode);
            break;

        case SquelchMode::SQUELCH_CCI:
            value = (int8_t)(this->driver.getQualityCci(qualityMode));
            break;

        default:
            return;
    }

    this->squelch.process(value);
}

void
Tuner::handleAutosquelch()
{
    if (this->autosquelch.getMode() == SQUELCH_NONE)
    {
        return;
    }

    this->autosquelch.setTimeout(this->driver.getMode() == MODE_AM ? 1 : autosquelchTimeout);
    this->autosquelch.process(this->driver.getSquelch() ? 1 : 0);
}

bool
Tuner::cbCancel(Controller *instance,
             const char *args)
{
    Tuner *tuner = (Tuner*)instance;
    (void)args;

    /* Cancel any ongoing asynchronous operation */

    /* Currently only scan is asynchronous */
    tuner->scan.stop();

    return true;
}

bool
Tuner::cbMode(Controller *instance,
              const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    tuner->cbCancel(instance, NULL);
    const Mode value = (Mode)atol(args);

    const Mode prevMode = tuner->driver.getMode();
    const uint32_t prevFrequency = tuner->driver.getFrequency();
    const uint32_t prevStep = tuner->driver.getStep();

    if (prevMode == MODE_FM || prevMode == MODE_AM)
    {
        tuner->lastFrequency[prevMode] = prevFrequency;
    }

    bool ok;
    if ((value == MODE_FM || value == MODE_AM) &&
        tuner->lastFrequency[value])
    {
        ok = tuner->driver.setFrequency(tuner->lastFrequency[value],
                                        TunerDriver::TUNE_DEFAULT);
    }
    else
    {
        ok = tuner->driver.setMode(value);
    }

    if (ok)
    {
        tuner->clear();
        tuner->feedback(FMDX_TUNER_PROTOCOL_MODE, value);

        const uint32_t newFrequency = tuner->driver.getFrequency();
        const uint32_t newStep = tuner->driver.getStep();
        if (prevFrequency != newFrequency ||
            prevStep != newStep)
        {
            tuner->feedback2(FMDX_TUNER_PROTOCOL_TUNE, newFrequency, newStep);
        }

        return true;
    }

    return false;
}

bool
Tuner::cbFrequency(Controller *instance,
                   const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    tuner->cbCancel(instance, NULL);
    const uint32_t value = atol(args);

    const Mode prevMode = tuner->driver.getMode();
    const uint32_t prevFrequency = tuner->driver.getFrequency();
    if (tuner->driver.setFrequency(value, TunerDriver::TUNE_DEFAULT))
    {
        const uint32_t newFrequency = tuner->driver.getFrequency();
        const uint32_t step = tuner->driver.getStep();
        const int32_t diff = prevFrequency - newFrequency;
        if (abs(diff) > step)
        {
            tuner->clear();
        }

        const uint32_t newMode = tuner->driver.getMode();
        if (prevMode != newMode)
        {
            tuner->feedback(FMDX_TUNER_PROTOCOL_MODE, newMode);
        }

        tuner->feedback2(FMDX_TUNER_PROTOCOL_TUNE, newFrequency, step);

        const uint32_t newAlignment = tuner->driver.getAlignment();
        tuner->feedback(FMDX_TUNER_PROTOCOL_ALIGNMENT, newAlignment);

        const Mode curMode = tuner->driver.getMode();
        if (curMode == MODE_FM || curMode == MODE_AM)
        {
            tuner->lastFrequency[curMode] = newFrequency;
        }
        if ((prevMode == MODE_FM || prevMode == MODE_AM) &&
            prevMode != curMode)
        {
            tuner->lastFrequency[prevMode] = prevFrequency;
        }

        return true;
    }

    return false;
}

bool
Tuner::cbDeemphasis(Controller *instance,
                    const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    tuner->cbCancel(instance, NULL);
    const uint32_t value = atol(args);

    if (tuner->driver.setDeemphasis(value))
    {
        tuner->feedback(FMDX_TUNER_PROTOCOL_DEEMPHASIS, value);
        return true;
    }

    return false;
}

bool
Tuner::cbAgc(Controller *instance,
             const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    tuner->cbCancel(instance, NULL);
    const uint32_t value = atol(args);

    if (tuner->driver.setAgc(value))
    {
        tuner->feedback(FMDX_TUNER_PROTOCOL_AGC, value);
        return true;
    }

    return false;
}

bool
Tuner::cbAlignment(Controller *instance,
                   const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    tuner->cbCancel(instance, NULL);
    const uint32_t value = atol(args);

    if (tuner->driver.setAlignment(value))
    {
        tuner->feedback(FMDX_TUNER_PROTOCOL_ALIGNMENT, tuner->driver.getAlignment());
        return true;
    }

    return false;
}

bool
Tuner::cbBandwidth(Controller *instance,
                   const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    tuner->cbCancel(instance, NULL);
    const uint32_t value = atol(args);

    if (tuner->driver.setBandwidth(value))
    {
        tuner->feedback(FMDX_TUNER_PROTOCOL_BANDWIDTH, tuner->driver.getBandwidth());
        return true;
    }

    return false;
}

bool
Tuner::cbVolume(Controller *instance,
                const char *args)
{
    Tuner *tuner = (Tuner*)instance;
    /* No cbCancel() here: a volume change would cause
       a silent frequency revert mid-seek with
       no feedback to the host */
    const uint32_t value = atol(args);
    
    if (value <= 100)
    {
        tuner->volume.setVolume((uint8_t)value);

        tuner->feedback(FMDX_TUNER_PROTOCOL_VOLUME, value);
        return true;
    }
    
    return false;
}

bool
Tuner::cbSquelch(Controller *instance,
                 const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    tuner->cbCancel(instance, NULL);
    const int32_t value = atol(args);

    if (value < 0)
    {
        tuner->squelch.set(SQUELCH_STEREO, 1);
    }
    else if (value == 0)
    {
        tuner->squelch.set(SQUELCH_NONE, 0);
    }
    else
    {
        tuner->squelch.set(SQUELCH_RSSI, value);
    }

    tuner->feedback(FMDX_TUNER_PROTOCOL_SQUELCH, value);
    return true;
}

bool
Tuner::cbAutosquelch(Controller *instance,
                     const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    tuner->cbCancel(instance, NULL);
    const bool enable = (atol(args) != 0);

    if (enable)
    {
        tuner->autosquelch.set(SQUELCH_AUTO, 1);
    }
    else
    {
        tuner->autosquelch.set(SQUELCH_NONE, 0);
    }

    tuner->feedback(FMDX_TUNER_PROTOCOL_AUTOSQUELCH, enable ? 1 : 0);
    return true;
}

bool
Tuner::cbOutputMode(Controller *instance,
                    const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    tuner->cbCancel(instance, NULL);
    const OutputMode value = (OutputMode)atol(args);

    if (tuner->driver.setOutputMode(value))
    {
        tuner->feedback(FMDX_TUNER_PROTOCOL_OUTPUT_MODE, value);
        tuner->stereo = (value == OUTPUT_MODE_STEREO);
        return true;
    }

    return false;
}

bool
Tuner::cbQuality(Controller *instance,
                 const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    tuner->cbCancel(instance, NULL);
    const uint32_t value = atol(args);

    if (value <= 1000)
    {
        tuner->timerQuality.set(value);
        tuner->feedback(FMDX_TUNER_PROTOCOL_QUALITY, value);
        return true;
    }

    return false;
}

bool
Tuner::cbScan(Controller *instance,
              const char *args)
{
    Tuner *tuner = (Tuner*)instance;
    tuner->cbCancel(instance, NULL);

    switch (args[0])
    {
        case 'm':
        case '\0':
            tuner->scan.setRepeat(args[0] == 'm');
            return tuner->scan.start();

        case 'a':
            tuner->scan.setFrom(atol(args+1));
            return true;

        case 'b':
            tuner->scan.setTo(atol(args+1));
            return true;

        case 'c':
            tuner->scan.setStep(atol(args+1));
            return true;

        case 'w':
            tuner->scan.setBandwidth(atol(args+1));
            return true;

        case 'z':
            /* TODO: Antenna */
            break;
    }

    return false;
}

bool
Tuner::cbCustom(Controller *instance,
                const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    tuner->cbCancel(instance, NULL);

    if (tuner->driver.setCustom("custom", args))
    {
        Comm.print(FMDX_TUNER_PROTOCOL_CUSTOM);
        Comm.print(args);
        Comm.print('\n');
        return true;
    }

    return false;
}

bool
Tuner::cbSeek(Controller *instance,
              const char *args)
{
    Tuner *tuner = (Tuner*)instance;

    if (tuner->scan.isScanning())
    {
        return false;
    }

    /* If a seek is in progress, redirect it from
       wherever it currently is rather than cancelling */
    if (!tuner->scan.isSeeking())
    {
        tuner->cbCancel(instance, NULL);
    }

    /* 1 = seek down, 2 = seek up (matches Search_PE5PVB_Mode) */
    if (!args || (args[0] != '1' && args[0] != '2'))
    {
        return false;
    }

    return tuner->scan.startSeek(args[0] == '2');
}

void
Tuner::feedback(const char *message,
                uint32_t    value)
{
    Comm.print(message);
    Comm.print(value, DEC);
    Comm.print('\n');
}


void
Tuner::feedback2(const char *message,
                 uint32_t    value,
                 uint32_t    value2)
{
    Comm.print(message);
    Comm.print(value, DEC);
    Comm.print(",");
    Comm.print(value2, DEC);
    Comm.print('\n');
}

