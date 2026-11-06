// SPDX-License-Identifier: GPL-3.0-only
// The video record as configure() in capture.c builds it, with every choice that depends on the firmware made a
// parameter, so that src/firmware_probe.cpp can try layouts the host doesn't know yet. With the host's choices for a
// firmware (ps_probe_host_layout) it gives configure()'s record byte for byte: tests/probe_record_test.c holds the two
// together, so a change to configure() that the probe doesn't follow fails there. Portable.
#ifndef PROSPERO_PROBE_RECORD_H
#define PROSPERO_PROBE_RECORD_H
#include "encoder_qp.h"
#include "experimental.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Slices per H.264 frame the host asks of 10.x, as PS_H264_SLICES in capture.c (tests/probe_record_test.c holds the two
// together).
#define PS_PROBE_H264_SLICES 4

typedef struct {
    // Bytes the 0xb8 block and the record differ from 13.00's (0xe0 and 0x2e0); everything 13.00 keeps from 0x198 on
    // moves by it. 13.00: 0; 13.60: +8; 10.x: -0x18; 9.60: -0x20.
    ptrdiff_t shift;
    int inner_headers;           // 13.00's sub-block headers inside the 0xb8 block (0xc0, 0xe0, 0x100, 0x120)
    int rate_fields;             // 13.00's average, peak and bits per frame at 0xc8, 0xcc and 0xd0
    uint32_t qp_idr, qp_other;   // the QP fields at ps_qp_offset(), 0 = not written; only without rate fields
    uint32_t slices;             // 10.x, H.264: slices per frame at 0x44, with 0x40 and 0x48 on; 0 = not written
    int hevc_fields;             // Remote Play's HEVC fields as configure() writes them on 13.00
    ptrdiff_t hevc_high;         // where those from 0x220 on sit: 0, or the shift for the full variant
    uint32_t purpose, resolution, timing, ten_bit, codec, resource, video_out_mode;
    uint32_t average, peak;      // bit/s
} ps_probe_layout;

static inline void ps_probe_put32(uint8_t *p, size_t at, uint32_t value) { memcpy(p + at, &value, 4); }
static inline void ps_probe_put64(uint8_t *p, size_t at, uint64_t value) { memcpy(p + at, &value, 8); }

// What configure() chooses on a firmware with this shift, for an HEVC variant as in experimental.h.
static inline ps_probe_layout ps_probe_host_layout(ptrdiff_t shift, uint32_t codec, uint32_t resolution, uint32_t timing,
                                                   uint32_t ten_bit, uint32_t resource, uint32_t video_out_mode,
                                                   uint32_t average, uint32_t peak, int variant)
{
    ps_probe_layout l;
    memset(&l, 0, sizeof l);
    l.shift = shift;
    l.inner_headers = l.rate_fields = shift >= 0;
    l.hevc_fields = shift >= 0 && codec == 1 && variant != PS_EXP_MINIMAL;
    l.hevc_high = variant == PS_EXP_FULL ? shift : 0;
    l.slices = shift < 0 && codec == 0 ? PS_PROBE_H264_SLICES : 0;
    l.purpose = codec == 1 && resolution >= 5 ? 6 : 2;
    l.resolution = resolution;
    l.timing = timing;
    l.ten_bit = ten_bit;
    l.codec = codec;
    l.resource = resource;
    l.video_out_mode = video_out_mode;
    l.average = average;
    l.peak = peak;
    return l;
}

// Bytes the record declares (its first word).
static inline size_t ps_probe_record_size(const ps_probe_layout *l) { return (size_t)(0x2e0 + l->shift); }

// Writes the record into p and returns its size, or 0 if it doesn't fit in capacity.
static inline size_t ps_probe_build_record(uint8_t *p, size_t capacity, const ps_probe_layout *l)
{
    static const struct { size_t at; uint64_t size; } headers[] = {
        {0, 0x2e0}, {8, 0x28}, {0x30, 0x58}, {0x38, 0x18},
        {0x50, 0x10}, {0x88, 0x30}, {0xb8, 0xe0}, {0xc0, 0x20},
        {0xe0, 0x20}, {0x100, 0x20}, {0x120, 0x18}, {0x1e8, 0xf8},
        {0x1f0, 0x28}, {0x218, 0x88}, {0x2a8, 0x10}
    };
    const ptrdiff_t s = l->shift;
    const ptrdiff_t high = l->hevc_fields ? l->hevc_high : 0;
    const ptrdiff_t end = 0x2e0 + s;
    if (end < 0x200 || (size_t)end > capacity || (l->hevc_fields && 0x2c8 + high > (ptrdiff_t)capacity)) return 0;
    memset(p, 0, capacity);
    for (size_t i = 0; i < sizeof headers / sizeof headers[0]; i++) {
        const size_t at = headers[i].at;
        if (!l->inner_headers && at > 0xb8 && at < 0x198) continue;
        ps_probe_put64(p, at >= 0x198 ? (size_t)((ptrdiff_t)at + s) : at,
                       at == 0 || at == 0xb8 ? (uint64_t)((ptrdiff_t)headers[i].size + s) : headers[i].size);
    }
    ps_probe_put32(p, 0x10, 0);
    ps_probe_put32(p, 0x14, l->purpose);
    ps_probe_put32(p, 0x18, l->resolution);
    ps_probe_put32(p, 0x1c, l->timing);
    ps_probe_put32(p, 0x20, l->ten_bit);
    ps_probe_put32(p, 0x24, l->codec);
    ps_probe_put32(p, 0x28, l->resource);
    const uint64_t fps = l->timing ? 60 : 30;
    ps_probe_put32(p, 0x94, (uint32_t)(fps * 10));
    if (l->rate_fields) {
        ps_probe_put32(p, 0xc8, l->average);
        ps_probe_put32(p, 0xcc, l->peak);
        ps_probe_put32(p, 0xd0, (uint32_t)((uint64_t)l->peak * 1001 / (fps * 1000)));
    } else {
        if (l->qp_idr) ps_probe_put32(p, ps_qp_offset(s), l->qp_idr);
        if (l->qp_other) ps_probe_put32(p, ps_qp_offset(s) + 4, l->qp_other);
    }
    if (l->slices) {
        ps_probe_put32(p, 0x40, 1);
        ps_probe_put32(p, 0x44, l->slices);
        ps_probe_put32(p, 0x48, 1);
    }
    ps_probe_put32(p, (size_t)(0x2a0 + s), l->video_out_mode);
    ps_probe_put32(p, (size_t)(0x2a4 + s), 2);
    ps_probe_put32(p, (size_t)(0x2b0 + s), 1);
    p[0x2b4 + s] = 1;
    if (l->hevc_fields) {
        ps_probe_put32(p, 0x44, 1);
        ps_probe_put32(p, 0x48, 1);
        p[0x58] = 1;
        ps_probe_put32(p, 0x5c, 9);
        ps_probe_put64(p, 0x60, 0x18);
        p[0x68] = 1;
        ps_probe_put32(p, 0x98, 1);
        ps_probe_put32(p, 0xd4, 35);
        ps_probe_put32(p, 0xd8, 0x33);
        p[0x108] = 1;
        // configure()'s budget per mode: bytes a frame may cost before the encoder skips frames (docs/re/hevc.md).
        static const uint32_t budgets[] = {0, 0x20000, 0x20000, 0x40000, 0x60000, 0xc0000, 0x100000};
        ps_probe_put32(p, 0x10c, budgets[l->resolution < 7 ? l->resolution : 0]);
        p[0x128] = 1;
        ps_probe_put32(p, 0x12c, 0x19);
        ps_probe_put64(p, 0x188, 0x10);
        p[0x190] = 1;
        ps_probe_put32(p, 0x194, 1);
        memset(p + 0x220 + high, 0xff, 0x80);
        ps_probe_put64(p, (size_t)(0x2b8 + high), 0x10);
        ps_probe_put32(p, (size_t)(0x2c0 + high), 1);
        ps_probe_put32(p, (size_t)(0x2c4 + high), 0x6e);
    }
    return (size_t)end;
}

#endif
