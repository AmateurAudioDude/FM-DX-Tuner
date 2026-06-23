/*  SPDX-License-Identifier: GPL-3.0-or-later
 *
 *  FM-DX Tuner
 *  Copyright (C) 2023  Konrad Kosmatka 
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

#ifndef FMDX_TUNER_SCAN_H
#define FMDX_TUNER_SCAN_H
#include "TunerDriver.hpp"
#include "Volume.hpp"
#include "../../Utils/Timer.hpp"

class Scan
{
public:
    Scan(TunerDriver &_driver, Volume &_volume);

    void setFrom(uint32_t value);
    uint32_t getFrom(void);

    void setTo(uint32_t value);
    uint32_t getTo(void);

    void setStep(uint32_t value);
    uint32_t getStep(void);

    void setBandwidth(uint32_t value);
    uint32_t getBandwidth(void);

    void setRepeat(bool value);
    bool getRepeat(void);

    bool isActive(void);
    bool isSeeking(void);
    bool isScanning(void);

    bool start(void);
    void stop(void);
    void process(void);

    bool startSeek(bool up);

private:
    void init(void);
    void next(void);
    void stopSeek(bool found);
    void seekWrap(void);
    bool seekQualityOk(void);

    enum State : uint8_t
    {
        None,
        Sample,
        Seek
    };

    TunerDriver &driver;
    Volume &volume;

    /* Configuration */
    uint32_t from;
    uint32_t to;
    uint32_t step;
    uint32_t bandwidth;
    bool repeat;

    /* Private data */
    uint32_t prevFrequency;
    uint32_t prevBandwidth;
    uint32_t current;
    State state;
    bool seekUp;

    /* Seek band edges and a safety cap on the total number of steps,
       so seek can't loop forever wrapping around an empty band */
    uint32_t seekLimitLow;
    uint32_t seekLimitHigh;
    uint32_t seekStepCount;
    uint32_t seekMaxSteps;

    /* Extra settle time at each frequency, independent of, and
       not limited by, the chip's own quality timestamp field */
    Timer seekSettle;

    /* Number of fresh samples taken at the current frequency
       so far, up to SEEK_MAX_ATTEMPTS before giving up */
    uint8_t seekAttempts;

    /* The very first step's T<freq> report is deferred until after
       the settle wait, rather than printed synchronously inside the
       same call that just finished reading the incoming command.
       Avoids a suspected USB CDC timing race unique to a write
       landing immediately after a read on the same interface */
    bool seekFirstReportPending;
};

#endif
