// SPDX-License-Identifier: GPL-3.0-only
// Every function a firmware check locks but that might work there anyway, one entry each: the name in sunshine.conf and
// host_diag.log, the words in the app, and the check for where it is verified. The app's Settings, the host's reading of
// sunshine.conf, its log lines and the diagnostics report all go through this list, so a new lock starts with a new entry
// here. What the function does on trial is the host's part: src/platform/ps5/experimental.h and capture.c.
// Plain C with nothing else included from the project, for the host (capture.c) and the app alike.
#ifndef PROSPERO_EXPERIMENTAL_LIST_H
#define PROSPERO_EXPERIMENTAL_LIST_H
#include <stddef.h>
#include <stdint.h>

// Firmware as kernel_get_fw_version() gives it: 0x13000040 is 13.00.
static inline int ps_exp_hevc_verified(uint32_t firmware)
{
    const uint32_t version = firmware & 0xffff0000u;
    return version == 0x13000000u || version == 0x10200000u;
}
static inline int ps_exp_large_verified(uint32_t firmware) { return (firmware & 0xffff0000u) == 0x13000000u; }
static inline int ps_exp_bitrate_verified(uint32_t firmware) { return (firmware & 0xffff0000u) == 0x13000000u; }
static inline int ps_exp_small_verified(uint32_t firmware)
{
    return (firmware & 0xffff0000u) == 0x10200000u || (firmware & 0xffff0000u) == 0x13000000u;
}

// A capture record without rate fields: 10.x's, and below 10 the still shorter one measured on 9.60 (record_shift in capture.c).
static inline int ps_exp_short_record(uint32_t firmware) { return firmware < 0x11000000u; }
static inline int ps_exp_never(uint32_t firmware) { (void)firmware; return 0; }

// X(id, key in sunshine.conf, name in host_diag.log, name in a notice, label on the tile, help on the tile,
//   where it is verified, where a switch could do nothing, the tile's word for that, the help for that)
#define PS_EXP_SWITCHES(X)                                                                                                     \
    X(HEVC, "experimental_hevc", "hevc", "HEVC", "HEVC (8 bit)",                                                               \
      "Offers HEVC on this firmware. If it fails, H.264 again until the host restarts.",                                       \
      ps_exp_hevc_verified, ps_exp_never, "", "")                                                                              \
    X(HDR, "experimental_hdr", "hdr", "HDR", "HDR (10 bit)",                                                                   \
      "Offers HDR, and HEVC with it, on this firmware. If it fails, off until the host restarts.",                             \
      ps_exp_hevc_verified, ps_exp_never, "", "")                                                                              \
    X(LARGE, "experimental_large_modes", "large", "1440p/4K", "1440p and 4K",                                                  \
      "Offers modes above 1080p on this firmware. If they fail, 1080p until the host restarts.",                               \
      ps_exp_large_verified, ps_exp_never, "", "")                                                                             \
    /* 10.x's record has no rate fields: the encoder keeps its own rate whatever is asked for. */                              \
    X(BITRATE, "experimental_high_bitrate", "bitrate", "high bitrate", "150 Mbit/s",                                           \
      "Raises the bitrate cap from 50 to 150 Mbit/s on this firmware. If it fails, 50 until the host restarts.",               \
      ps_exp_bitrate_verified, ps_exp_short_record, "No effect here",                                                          \
      "The encoder keeps its own rate on this firmware, so this switch would change nothing.")                                 \
    /* Verified on 10.20 and 13.00 (13.00 with its own HEVC budgets and a bitrate cap per mode, STATUS.md). */                 \
    X(SMALL, "experimental_small_modes", "small", "below 1080p", "Below 1080p",                                                \
      "Offers 720p, 540p and 360p to clients that ask for less than 1080p. If they fail, 1080p until the host restarts.",      \
      ps_exp_small_verified, ps_exp_never, "", "")

enum ps_exp_switch {
#define PS_EXP_ENUM(id, key, name, shown, label, help, verified, no_effect, no_effect_word, no_effect_help) PS_EXP_##id,
    PS_EXP_SWITCHES(PS_EXP_ENUM)
#undef PS_EXP_ENUM
    PS_EXP_SWITCH_COUNT
};

struct ps_exp_switch_info {
    const char *key, *name, *shown, *label, *help;
    int (*verified)(uint32_t firmware);
    int (*no_effect)(uint32_t firmware);
    const char *no_effect_word, *no_effect_help;
};

static inline const struct ps_exp_switch_info *ps_exp_switch_at(int index)
{
    static const struct ps_exp_switch_info table[PS_EXP_SWITCH_COUNT] = {
#define PS_EXP_ROW(id, key, name, shown, label, help, verified, no_effect, no_effect_word, no_effect_help) \
    {key, name, shown, label, help, verified, no_effect, no_effect_word, no_effect_help},
        PS_EXP_SWITCHES(PS_EXP_ROW)
#undef PS_EXP_ROW
    };
    return &table[index];
}

// Whether a switch can change anything on this firmware. Where not, the host ignores it and the app says why.
static inline int ps_exp_counts(int index, uint32_t firmware)
{
    const struct ps_exp_switch_info *info = ps_exp_switch_at(index);
    return !info->verified(firmware) && !info->no_effect(firmware);
}

#endif
