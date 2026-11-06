// SPDX-License-Identifier: GPL-3.0-only
// What the app's Experimental switches (app/src/psapp/experimental_list.h) unlock, and what follows from an attempt.
// Portable, tested in tests/experimental_test.cpp; capture.c holds the one instance. Background: docs/firmware-porting.md.
//
// A switch counts only where the function isn't verified, and the bitrate's not on 10.x, where the record has no rate
// fields. Each codec and bit depth tries its variants in order until one delivers a frame, and that one stays. When all
// fail, a rate above 50 Mbit/s falls back to 50, then a mode above or below 1080p to 1080p, each in the same stream and with
// the variants starting over. What was shown to be at fault is off until the host restarts. When nothing is left to try, the
// client's stream ends, because it set up its decoder for this codec, and its next stream gets what the host offers then.
#ifndef PROSPERO_EXPERIMENTAL_H
#define PROSPERO_EXPERIMENTAL_H
#include "../../../app/src/psapp/experimental_list.h"
#include <stdint.h>
#include <string.h>

enum ps_exp_variant {
    PS_EXP_STANDARD,  // what the host does without a switch
    PS_EXP_MINIMAL,   // HEVC with cfg+0x24 alone, as on 10.20
    PS_EXP_FULL,      // HEVC with 13.00's Remote Play fields and encoder memory
};

enum ps_exp_next {
    PS_EXP_GIVE_UP,       // the open has failed
    PS_EXP_NEXT_VARIANT,  // open again with the next variant
    PS_EXP_LOWER_RATE,    // open again at PS_EXP_SAFE_AVERAGE
    PS_EXP_FALL_BACK,     // open again at 1080p
};

// The cap without the switch.
#define PS_EXP_SAFE_AVERAGE 50000000u

typedef struct {
    uint8_t on[PS_EXP_SWITCH_COUNT];         // switches that count on this firmware
    uint8_t failed[PS_EXP_SWITCH_COUNT];     // until the host restarts
    uint8_t confirmed[PS_EXP_SWITCH_COUNT];  // it ran; not used by HEVC and HDR, which have `trial`
    uint8_t pending[PS_EXP_SWITCH_COUNT];    // it failed, and the safer setting has yet to show that it was at fault
    uint8_t last_variant;
    struct {
        uint8_t variant, confirmed;
    } trial[2];  // by bit depth: 8 and 10 bit
} ps_experiment;

static inline void ps_exp_init(ps_experiment *e, uint32_t firmware, const int switches[PS_EXP_SWITCH_COUNT])
{
    memset(e, 0, sizeof *e);
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; i++) e->on[i] = switches[i] && ps_exp_counts(i, firmware);
    // 10.x's validator refuses 13.00's inner fields (docs/re/hevc.md), and configure() leaves them out there: both are the same.
    e->last_variant = ps_exp_short_record(firmware) ? PS_EXP_MINIMAL : PS_EXP_FULL;
    e->trial[0].variant = e->trial[1].variant = PS_EXP_MINIMAL;
}

static inline int ps_exp_any(const ps_experiment *e)
{
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; i++)
        if (e->on[i]) return 1;
    return 0;
}

// What a switch adds to what the firmware has anyway. HDR is HEVC at 10 bit and can't be offered without it.
static inline int ps_exp_hevc_offered(const ps_experiment *e)
{
    return (e->on[PS_EXP_HEVC] || (e->on[PS_EXP_HDR] && !e->failed[PS_EXP_HDR])) && !e->failed[PS_EXP_HEVC];
}
static inline int ps_exp_hdr_offered(const ps_experiment *e) { return e->on[PS_EXP_HDR] && !e->failed[PS_EXP_HDR] && !e->failed[PS_EXP_HEVC]; }
static inline int ps_exp_large_offered(const ps_experiment *e) { return e->on[PS_EXP_LARGE] && !e->failed[PS_EXP_LARGE]; }
static inline int ps_exp_bitrate_offered(const ps_experiment *e) { return e->on[PS_EXP_BITRATE] && !e->failed[PS_EXP_BITRATE]; }
static inline int ps_exp_small_offered(const ps_experiment *e) { return e->on[PS_EXP_SMALL] && !e->failed[PS_EXP_SMALL]; }

// The switch a mode is on trial under: LARGE above 1080p, SMALL below, none at 1080p.
static inline int ps_exp_mode_switch(uint32_t resolution)
{
    return resolution >= 5 ? PS_EXP_LARGE : resolution < 4 ? PS_EXP_SMALL : -1;
}

static inline int ps_exp_codec_on_trial(const ps_experiment *e, uint32_t codec, uint32_t ten_bit)
{
    return codec == 1 && (ten_bit ? ps_exp_hdr_offered(e) : ps_exp_hevc_offered(e));
}

static inline int ps_exp_variant_for(const ps_experiment *e, uint32_t codec, uint32_t ten_bit)
{
    return ps_exp_codec_on_trial(e, codec, ten_bit) ? e->trial[ten_bit != 0].variant : PS_EXP_STANDARD;
}

// Whether this open still has to show that it works, so the first frame counts as a step of it.
static inline int ps_exp_unproven_codec(const ps_experiment *e, uint32_t codec, uint32_t ten_bit)
{
    return ps_exp_codec_on_trial(e, codec, ten_bit) && !e->trial[ten_bit != 0].confirmed;
}
static inline int ps_exp_unproven_mode(const ps_experiment *e, uint32_t resolution)
{
    const int i = ps_exp_mode_switch(resolution);
    return i >= 0 && e->on[i] && !e->failed[i] && !e->confirmed[i];
}

// Whether a refused open of this mode may go on at 1080p in the same stream: every mode but 1080p itself. A larger one,
// because titles refuse 4K; a smaller one, because 1080p is what every client got before the smaller modes, which beats no
// stream. Only a switch on trial goes off for it (ps_exp_failed); a verified or proven mode is tried again next stream.
static inline int ps_exp_mode_may_fall_back(uint32_t resolution)
{
    return resolution != 4;
}

static inline int ps_exp_unproven_rate(const ps_experiment *e, uint32_t average)
{
    return average > PS_EXP_SAFE_AVERAGE && ps_exp_bitrate_offered(e) && !e->confirmed[PS_EXP_BITRATE];
}

static inline void ps_exp_succeeded(ps_experiment *e, uint32_t codec, uint32_t ten_bit, uint32_t resolution, uint32_t average)
{
    if (ps_exp_codec_on_trial(e, codec, ten_bit)) e->trial[ten_bit != 0].confirmed = 1;
    const int mode = ps_exp_mode_switch(resolution);
    if (mode >= 0 && e->on[mode]) e->confirmed[mode] = 1;
    if (e->pending[PS_EXP_LARGE] && mode != PS_EXP_LARGE) e->failed[PS_EXP_LARGE] = 1;
    if (e->pending[PS_EXP_SMALL] && mode != PS_EXP_SMALL) e->failed[PS_EXP_SMALL] = 1;
    e->pending[PS_EXP_LARGE] = e->pending[PS_EXP_SMALL] = 0;
    if (average > PS_EXP_SAFE_AVERAGE && e->on[PS_EXP_BITRATE]) e->confirmed[PS_EXP_BITRATE] = 1;
    if (e->pending[PS_EXP_BITRATE] && average <= PS_EXP_SAFE_AVERAGE) e->failed[PS_EXP_BITRATE] = 1;
    e->pending[PS_EXP_BITRATE] = 0;
}

// `attributable`: the failure came from the encoder's steps, not from the capture being in use or the system refusing the
// resource, which says nothing about the function. `can_fall_back`: ps_capture_fall_back_mode would act.
static inline enum ps_exp_next ps_exp_failed(ps_experiment *e, uint32_t codec, uint32_t ten_bit, uint32_t resolution, uint32_t average,
                                             int attributable, int can_fall_back)
{
    const int mode = ps_exp_mode_switch(resolution);
    const int bits = ten_bit != 0;
    const int trial = attributable && ps_exp_unproven_codec(e, codec, ten_bit);
    if (trial && e->trial[bits].variant < e->last_variant) {
        e->trial[bits].variant++;
        return PS_EXP_NEXT_VARIANT;
    }
    if (attributable && average > PS_EXP_SAFE_AVERAGE && ps_exp_bitrate_offered(e)) {
        if (trial) e->trial[bits].variant = PS_EXP_MINIMAL;
        if (!e->confirmed[PS_EXP_BITRATE]) e->pending[PS_EXP_BITRATE] = 1;
        return PS_EXP_LOWER_RATE;
    }
    // The lower rate failed as well: nothing shows that the rate was at fault.
    e->pending[PS_EXP_BITRATE] = 0;
    if (can_fall_back) {
        if (trial) e->trial[bits].variant = PS_EXP_MINIMAL;
        if (attributable && mode >= 0 && e->on[mode] && !e->confirmed[mode]) e->pending[mode] = 1;
        return PS_EXP_FALL_BACK;
    }
    if (trial) {
        if (bits)
            e->failed[PS_EXP_HDR] = 1;
        else
            e->failed[PS_EXP_HEVC] = 1;
    }
    e->pending[PS_EXP_LARGE] = e->pending[PS_EXP_SMALL] = 0;
    return PS_EXP_GIVE_UP;
}

#endif
