#ifndef FMDX_TUNER_CONFIG_SEEK_H
#define FMDX_TUNER_CONFIG_SEEK_H

/* -----------------------------------------------------------------------
   Seek configuration
   ----------------------------------------------------------------------- */
/* Step size [kHz] used while seeking (independent of the manual tuning
   step). */
#define SEEK_STEP_FM 100
#define SEEK_LIMIT_FM_LOW  87000
#define SEEK_LIMIT_FM_HIGH 108000

#define SEEK_STEP_OIRT 30
#define SEEK_LIMIT_OIRT_LOW  64000
#define SEEK_LIMIT_OIRT_HIGH 74000

/* LW/MW. */
#define SEEK_AM_LOW_STEP 9

#define SEEK_LIMIT_LW_LOW  144
#define SEEK_LIMIT_LW_HIGH 513

#if SEEK_AM_LOW_STEP == 10
#define SEEK_LIMIT_MW_LOW  520
#define SEEK_LIMIT_MW_HIGH 1720
#else
#define SEEK_LIMIT_MW_LOW  522
#define SEEK_LIMIT_MW_HIGH 1791
#endif

#define SEEK_STEP_SW 5
#define SEEK_LIMIT_SW_LOW  1800
#define SEEK_LIMIT_SW_HIGH 27000

/* Settling delay before a quality sample is trusted while seeking.
   Per the TEF668X manual, the chip's own quality timestamp counts
   linearly up to 320 (32 ms). */
#define SEEK_QUALITY_DELAY 40

/* Additional settle time [ms] at each frequency, on top of the
   chip's own 32 ms minimum, before the squelch decision is made.
   Unlike SEEK_QUALITY_DELAY this is a real, uncapped firmware
   timer, not limited by the chip's own timestamp field. */
#define SEEK_EXTRA_SETTLE_MS 60

/* Number of independent, freshly-sampled attempts given to each
   frequency before giving up on it. Any single passing sample
   accepts immediately. 1 = single-pass behaviour. Raising this gives
   a flickering real station (with co-channel interference) more chances
   to be caught, but by the same logic also gives a ghost frequency
   more independent chances to randomly produce one passing sample. */
#define SEEK_MAX_ATTEMPTS 2
/* ----------------------------------------------------------------------- */


/* -----------------------------------------------------------------------
   Seek quality thresholds

   All measured ranges below are from real-world testing and
   mileage may vary.
   ----------------------------------------------------------------------- */
/* Minimum signal level [dBf * 100]. May help prevents stopping on
   ghost frequencies with no real carrier, where the other readings
   below can otherwise look deceptively clean. */
#define SEEK_MIN_LEVEL 300

/* FM noise (USN) ceiling. Measured: empty ~100-250, weak station
   ~30-150, moderate ~20-100, strong <10. Heavy overlap with weak
   stations exists in the 100-150 region. This is a compromise, not a
   clean cutoff on its own. */
#define SEEK_USN_MAX 120

/* FM co-channel (WAM) ceiling, raw chip units (not the same scale as
   the CCI value, which is WAM clamped to 32-360 and rescaled to 0-100%).
   Measured: empty ~270-500, weak station ~200-480, moderate ~40-100,
   strong ~25-35. Mainly useful for rejecting moderate/strong-but-not-real
   readings. Combined with USN and ACI below for the weak-station case. */
#define SEEK_WAM_MAX 280

/* FM adjacent-channel (ACI) ceiling, 0-100%, derived from adaptive
   bandwidth narrowing (TEF668X::getQualityAci()), a different
   mechanism to USN/WAM, not redundant with either. Measured: empty
   reads exactly 100 consistently. Weak real stations capped at
   98-99. This is the cleanest separation found, but only valid when
   bandwidth is set to auto (0) - returns -1 and is skipped otherwise. */
#define SEEK_ACI_MAX 99

/* FM frequency offset ceiling [*0.1 kHz]. Not useful for telling
   empty apart from real on-channel signals, but a strong station
   bleeding through from 100 kHz away reads dramatically more extreme
   (~200-500 either side). */
#define SEEK_OFFSET_MAX 150
/* ----------------------------------------------------------------------- */

#endif
