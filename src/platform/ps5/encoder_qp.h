// SPDX-License-Identifier: GPL-3.0-only
// The QP the encoder uses on firmware 10.x and 9.60. Its capture record has no rate fields (capture.c), and the encoder holds one QP
// for IDR frames (cfg+0xc4) and one for the others (cfg+0xc8). Left alone it sent 0.3 to 0.4 Mbit/s in a moving game,
// whatever the client asked for, which shows as blocks. sceAvcap2SetVideoBitrate doesn't help there: the next read fails
// with 0x80c78000. So the QP follows the client's bitrate instead.
//
// Measured on 10.20 in GTA III, driving fast, HEVC 10 bit at 1080p60 (STATUS.md), average and busiest second:
// 30/34 about 2 and 3.1 Mbit/s, 27/31 2.4 and 3.7, 24/28 3 and 5.1, 21/25 5.3 and 10. Each step starts at about twice
// its busiest second, as busier games will need more; the encoder still doesn't hold the client's bitrate.
// Portable, tested in tests/encoder_qp_test.cpp; configure() in capture.c applies it.
#ifndef PROSPERO_ENCODER_QP_H
#define PROSPERO_ENCODER_QP_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t idr, other;
} ps_qp;

// Where the IDR's QP sits, the others' 4 bytes on, for a record that moves by `shift`. 9.60 (firmware probe, phase ratesearch): 20 at
// 0xac gave 1.9 times the bytes, at 0xb0 1.6 times, and nothing changed at 0xc4 and 0xc8.
static inline size_t ps_qp_offset(ptrdiff_t shift) { return shift == -0x20 ? 0xac : 0xc4; }

// bitrate: the client's bitrate in bit/s, the average ps_capture_set_opening_bitrate() received.
static inline ps_qp ps_qp_for_bitrate(uint64_t bitrate)
{
    const uint32_t idr = bitrate >= 20000000u ? 21 : bitrate >= 12000000u ? 24 : bitrate >= 8000000u ? 27 : 30;
    const ps_qp qp = {idr, idr + 4};
    return qp;
}

#endif
