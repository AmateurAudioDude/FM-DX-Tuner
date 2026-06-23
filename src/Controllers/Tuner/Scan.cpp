/*  SPDX-License-Identifier: GPL-3.0-or-later
 *
 *  FM-DX Tuner
 *  Copyright (C) 2023-2024  Konrad Kosmatka
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
#include "Scan.hpp"
#include "TunerDriver.hpp"
#include "../../Utils/Utils.hpp"
#include "../../Protocol.h"
#include "../../../Config.hpp"
#include "../../../ConfigSeek.hpp"

Scan::Scan(TunerDriver &_driver, Volume &_volume)
    : driver(_driver), volume(_volume)
{
}

void
Scan::setFrom(uint32_t value)
{
    this->from = value;
}

uint32_t
Scan::getFrom(void)
{
    return this->from;
}

void
Scan::setTo(uint32_t value)
{
    this->to = value;
}

uint32_t
Scan::getTo(void)
{
    return this->to;
}

void
Scan::setStep(uint32_t value)
{
    this->step = value;
}

uint32_t
Scan::getStep(void)
{
    return this->step;
}

void
Scan::setBandwidth(uint32_t value)
{
    this->bandwidth = value;
}

uint32_t
Scan::getBandwidth(void)
{
    return this->bandwidth;
}

void
Scan::setRepeat(bool value)
{
    this->repeat = value;
}

bool
Scan::getRepeat(void)
{
    return this->repeat;
}

bool
Scan::isActive()
{
    return (this->state != None);
}

bool
Scan::isSeeking()
{
    return (this->state == Seek);
}

bool
Scan::isScanning()
{
    return (this->state == Sample);
}

bool
Scan::start()
{
    if (this->from == 0 ||
        this->to == 0 ||
        this->step == 0 ||
        this->from > this->to)
    {
        return false;
    }

    this->prevFrequency = this->driver.getFrequency();
    this->prevBandwidth = this->driver.getBandwidth();
        
    this->volume.mute();
    this->driver.setFrequency(this->from, TunerDriver::TUNE_SCAN);
    this->driver.setBandwidth(this->bandwidth);
    this->driver.resetQuality();
    
    /* Update bandwidth to the value set by driver */
    this->bandwidth = this->driver.getBandwidth();

    this->init();
    return true;
}

void
Scan::stop()
{
    if (this->state == None)
    {
        return;
    }

    this->driver.setFrequency(this->prevFrequency, TunerDriver::TUNE_DEFAULT);
    this->driver.setBandwidth(this->prevBandwidth);
    this->driver.resetQuality();
    this->driver.resetRds();
    this->volume.unMute();
    this->state = None;

    Comm.print('\n');
}

void
Scan::process()
{
    if (this->state == None ||
        !this->driver.getQuality())
    {
        return;
    }

    if (this->state == Seek)
    {
        if (!this->seekSettle.process(Timer::Oneshot))
        {
            /* Still within the extra settle window
               for this frequency, nothing to do yet */
            return;
        }

        if (this->seekFirstReportPending)
        {
            this->seekFirstReportPending = false;
            Comm.print(FMDX_TUNER_PROTOCOL_TUNE);
            Comm.print(this->current, DEC);
            Comm.print('\n');
        }

        if (this->seekQualityOk())
        {
            this->stopSeek(true);
            return;
        }

        if (++this->seekAttempts < SEEK_MAX_ATTEMPTS)
        {
            /* Give this frequency another fresh,
               independent sample before moving on */
            this->driver.resetQuality();
            this->seekSettle.set(SEEK_EXTRA_SETTLE_MS);
            return;
        }

        this->seekAttempts = 0;

        if (++this->seekStepCount > this->seekMaxSteps)
        {
            /* Wrapped around the configured band without
               finding anything, give up rather than loop */
            this->stopSeek(false);
            return;
        }

        this->current = this->seekUp ? (this->current + this->step) : (this->current - this->step);
        this->seekWrap();

        if (!this->driver.setFrequency(this->current, TunerDriver::TUNE_SCAN | TunerDriver::TUNE_SEEK))
        {
            this->stopSeek(false);
            return;
        }

        this->driver.resetQuality();
        this->seekSettle.set(SEEK_EXTRA_SETTLE_MS);

        /* Report each intermediate step, not just the final landing
           frequency, so the host can show the scan moving in real time */
        Comm.print(FMDX_TUNER_PROTOCOL_TUNE);
        Comm.print(this->current, DEC);
        Comm.print('\n');
        return;
    }

    TunerDriver::QualityMode mode = TunerDriver::QUALITY_FAST;

    Comm.print(this->driver.getFrequency(), DEC);
    Comm.print('=');
    Utils::serialDecimal(this->driver.getQualityRssi(mode), 2);

    this->next();
}

bool
Scan::startSeek(bool up)
{
    switch (this->driver.getMode())
    {
        case MODE_FM:
            this->step = SEEK_STEP_FM;
            this->seekLimitLow = SEEK_LIMIT_FM_LOW;
            this->seekLimitHigh = SEEK_LIMIT_FM_HIGH;
            break;

        case MODE_AM:
            this->step = SEEK_STEP_AM;
            this->seekLimitLow = SEEK_LIMIT_AM_LOW;
            this->seekLimitHigh = SEEK_LIMIT_AM_HIGH;
            break;

        default:
            return false;
    }

    this->prevFrequency = this->driver.getFrequency();
    this->prevBandwidth = this->driver.getBandwidth();
    this->seekUp = up;
    this->seekStepCount = 0;
    this->seekAttempts = 0;
    this->seekMaxSteps = ((this->seekLimitHigh - this->seekLimitLow) / this->step + 1) * 2;
    this->current = up ? (this->prevFrequency + this->step) : (this->prevFrequency - this->step);
    this->seekWrap();

    if (!this->driver.setFrequency(this->current, TunerDriver::TUNE_SCAN | TunerDriver::TUNE_SEEK))
    {
        return false;
    }

    /* Only mute if not already seeking, redirecting an in-progress
       seek into a new direction must not increment the mute ref
       count a second time */
    if (this->state != Seek)
    {
        this->volume.mute();
    }

    this->driver.resetQuality();
    this->seekSettle.set(SEEK_EXTRA_SETTLE_MS);
    this->state = Seek;

    /* Reported after the settle wait in process(), not
       immediately here, see seekFirstReportPending */
    this->seekFirstReportPending = true;
    return true;
}

void
Scan::seekWrap(void)
{
    if (this->seekUp && this->current > this->seekLimitHigh)
    {
        this->current = this->seekLimitLow;
    }
    else if (!this->seekUp && this->current < this->seekLimitLow)
    {
        this->current = this->seekLimitHigh;
    }
}

bool
Scan::seekQualityOk(void)
{
    /* Directly from the raw quality readings,
       compared against Seek's own thresholds. */
    constexpr TunerDriver::QualityMode mode = TunerDriver::QUALITY_FAST;

    if (this->driver.getQualityRssi(mode) < SEEK_MIN_LEVEL)
    {
        return false;
    }

    if (this->driver.getQualityNoise(mode) >= SEEK_USN_MAX)
    {
        return false;
    }

    if (this->driver.getQualityCoChannel(mode) >= SEEK_WAM_MAX)
    {
        return false;
    }

    /* ACI is only valid with auto bandwidth, skip
       check rather than treating -1 as a pass or fail */
    const int16_t aci = this->driver.getQualityAci(mode);
    if (aci != -1 && aci >= SEEK_ACI_MAX)
    {
        return false;
    }

    /* Wide to catch strong-station bleed-through
       from a neighbouring channel, not to fight the
       empty-vs-real overlap */
    const int16_t offset = this->driver.getQualityOffset(mode);
    if (offset <= -SEEK_OFFSET_MAX || offset >= SEEK_OFFSET_MAX)
    {
        return false;
    }

    return true;
}

void
Scan::stopSeek(bool found)
{
    if (!found)
    {
        this->driver.setFrequency(this->prevFrequency, TunerDriver::TUNE_DEFAULT);
    }

    this->driver.resetQuality();
    this->driver.resetRds();
    this->volume.unMute();
    this->state = None;

    Comm.print(FMDX_TUNER_PROTOCOL_TUNE);
    Comm.print(this->driver.getFrequency(), DEC);
    Comm.print('\n');
}

void
Scan::init(void)
{
    /* TODO: Due to asynchronous nature of scanning,
       the message will be changed in new protocol */
    Comm.print('U');
    /* Currently, it can be easily corrupted by another
       message coming from a different controller */

    this->current = this->from;
    this->state = Sample;
}

void
Scan::next(void)
{
    this->current += this->step;

    if (this->current > this->to)
    {
        if (!this->repeat)
        {
            this->stop();
            return;
        }

        Comm.print('\n');
        this->init();
    }

    this->driver.setFrequency(this->current, TunerDriver::TUNE_SCAN);
    this->driver.resetQuality();
    Comm.print(',');
}
