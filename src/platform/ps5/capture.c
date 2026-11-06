// SPDX-License-Identifier: GPL-3.0-only
// Derived from ps5-mcp/payload/avcap_probe/avcap_probe.c, FW 13.00 research.
#include "capture.h"
#include "diag.h"
#include "encoder_qp.h"
#include "experimental.h"
#include "ra_resolve.h"
#include "video/nal_slices.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ps5/kernel.h>

// Two channels can run at once (dual_open_probe, STATUS.md): slot 0 on profile 180, 182 above 1080p, and slot 1 on 175, up
// to 1080p; the arbitrator refuses 185 next to 180. Each slot has its own settings for the next open, its own session and
// its own answers from the arbitrator; slot 0 does what the single channel did before.
// The resource the next open asks for, and the one of the session opened last, which events and the config refer to.
static _Atomic int resource_id[PS_CAPTURE_SLOTS] = {180, 175}, session_resource[PS_CAPTURE_SLOTS] = {180, 175};
#define AGAIN ((int)0x81950002)
#define RESOURCE_NOT_ACTIVE ((int)0x81950008)
// Slices per H.264 frame asked of 10.x (cfg+0x44), well below what the decoders take.
#define PS_H264_SLICES 4
#define LOG(...) ps_diag_log("ProsperoSunshine: capture " __VA_ARGS__)
// Slot 1's lines say "capture2": the app's report reads slot 0's "capture" lines as the outcome of the host's capture.
#define SLOG(slot, format, ...) ps_diag_log("ProsperoSunshine: capture%s " format, (slot) ? "2" : "", ##__VA_ARGS__)
static int valid_slot(int slot) { return slot >= 0 && slot < PS_CAPTURE_SLOTS; }
typedef int (*load_fn)(const char *, size_t, const void *, uint32_t, void *, int *);
typedef int (*handle_fn)(intptr_t);

struct ps_capture {
    int slot;
    intptr_t handle;
    int registered, requested, initialized, opened, started;
    int resource;  // close and audio must use what this session registered, whatever is set for the next open
    int (*unregister)(int);
    int (*request)(int, const char *, void *, int);
    int (*terminate)(void);
    handle_fn stop, close;
    int (*headers)(intptr_t, void *);
    int (*read)(intptr_t, void *, size_t, void *, uint32_t);
    int (*read2)(intptr_t, void *, size_t, void *, uint32_t);
    // Optional calls (docs/re/avcap2-exports.md), resolved once at open: kernel_dynlib_dlsym is not
    // safe to call from several threads at once, and the video, audio and control threads all use these.
    handle_fn request_keyframe, start;
    int (*set_bitrate)(intptr_t, void *);
    int (*open_audio)(intptr_t *, const void *, uint8_t);
    int (*read_audio)(intptr_t, void *, size_t, void *, uint32_t);
    // Audio channel (docs/re/avcap2-exports.md, R2); shares the video session's resource grant.
    intptr_t audio;
    int audio_opened, audio_started;
    // HEVC only: encoder memory reserved like Remote Play does.
    int (*free_memory)(void *, void *);
    uint8_t memory[0x40];
    int has_memory;
    uint32_t mode[7];  // what mode_snapshot() returned at open
};

// Callback ABI currently has no verified instance context, so resource ownership
// is explicitly exclusive per slot. The callback touches only process-lifetime atomics and tells the slots apart by the
// resource, as dual_open_probe does: each slot registers its own.
static atomic_flag owned[PS_CAPTURE_SLOTS] = {ATOMIC_FLAG_INIT, ATOMIC_FLAG_INIT};
static atomic_int granted[PS_CAPTURE_SLOTS];
static atomic_int revoked[PS_CAPTURE_SLOTS];
static atomic_int refused[PS_CAPTURE_SLOTS];
static atomic_int refused_value[PS_CAPTURE_SLOTS];  // data[0] of the answer that refused
// The last open was refused while the output had no mode or changed it; the next opens reopen a running stream's capture.
static atomic_int unsettled[PS_CAPTURE_SLOTS], reopening[PS_CAPTURE_SLOTS];
static int resource_event(int id, int event, const uint8_t *data)
{
    for (int slot = 0; slot < PS_CAPTURE_SLOTS; slot++) {
        if (id != atomic_load(&session_resource[slot])) continue;
        if (event == 1 && data) {
            atomic_store(&granted[slot], data[0] == 1);
            if (data[0] != 1) {
                atomic_store(&refused_value[slot], data[0]);
                atomic_store(&refused[slot], 1);
            }
        }
        if (event == 2) {
            atomic_store(&revoked[slot], 1);
            atomic_store(&granted[slot], 0);
        }
    }
    return 0;
}

// Opens one at a time: kernel_dynlib_dlsym is not safe from several threads at once, and the ResourceArbitrator calls are
// resolved on the first open. Avcap2 is initialized once for both channels and terminated when the last one closes, as
// dual_open_probe ran them.
static pthread_mutex_t open_lock = PTHREAD_MUTEX_INITIALIZER, init_lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized_sessions;  // under init_lock

static uint32_t module(load_fn load, const char *name)
{
    uint32_t handle = 0;
    if (kernel_dynlib_handle(-1, name, &handle) == 0 && handle) return handle;
    char path[128];
    int result = -1;
    snprintf(path, sizeof path, "/system/common/lib/%s", name);
    int rc = load(path, 0, NULL, 0, NULL, &result);
    LOG("load %s rc=%#x result=%#x\n", name, rc, result);
    if (kernel_dynlib_handle(-1, name, &handle)) return 0;
    return handle;
}

static void u32(uint8_t *p, size_t at, uint32_t value) { memcpy(p + at, &value, 4); }
static void u64(uint8_t *p, size_t at, uint64_t value) { memcpy(p + at, &value, 8); }

static _Atomic uint32_t video_resolution[PS_CAPTURE_SLOTS] = {4, 4}, video_timing[PS_CAPTURE_SLOTS] = {1, 1},
                        video_codec[PS_CAPTURE_SLOTS], video_ten_bit[PS_CAPTURE_SLOTS];
static _Atomic int last_open_rc = 0;

static _Atomic uint32_t opening_average[PS_CAPTURE_SLOTS] = {6000000, 6000000}, opening_peak[PS_CAPTURE_SLOTS];

void ps_capture_slot_set_opening_bitrate(int slot, uint32_t average, uint32_t peak)
{
    if (!valid_slot(slot)) return;
    atomic_store(&opening_average[slot], average);
    atomic_store(&opening_peak[slot], peak);
}

void ps_capture_set_opening_bitrate(uint32_t average, uint32_t peak) { ps_capture_slot_set_opening_bitrate(0, average, peak); }

uint32_t ps_capture_slot_opening_average(int slot) { return valid_slot(slot) ? atomic_load(&opening_average[slot]) : 0; }

uint32_t ps_capture_opening_average(void) { return ps_capture_slot_opening_average(0); }

int ps_capture_slot_set_codec(int slot, uint32_t codec)
{
    if (!valid_slot(slot) || codec > 1) return PS_CAPTURE_ERROR;
    atomic_store(&video_codec[slot], codec);
    return 0;
}

int ps_capture_set_codec(uint32_t codec) { return ps_capture_slot_set_codec(0, codec); }

void ps_capture_slot_set_ten_bit(int slot, uint32_t ten_bit)
{
    if (valid_slot(slot)) atomic_store(&video_ten_bit[slot], ten_bit != 0);
}

void ps_capture_set_ten_bit(uint32_t ten_bit) { ps_capture_slot_set_ten_bit(0, ten_bit); }

int ps_capture_last_open_rc(void) { return atomic_load(&last_open_rc); }

int ps_capture_hevc_verified(void) { return ps_exp_hevc_verified(kernel_get_fw_version()); }

int ps_capture_large_modes_verified(void) { return ps_exp_large_verified(kernel_get_fw_version()); }

int ps_capture_high_bitrate_verified(void) { return ps_exp_bitrate_verified(kernel_get_fw_version()); }

int ps_capture_small_modes_verified(void) { return ps_exp_small_verified(kernel_get_fw_version()); }

// The Experimental switches (experimental.h). The lock also covers what the failure handling below changes.
static pthread_mutex_t experiment_lock = PTHREAD_MUTEX_INITIALIZER;
static ps_experiment experiment;

static ps_experiment experiment_now(void)
{
    pthread_mutex_lock(&experiment_lock);
    const ps_experiment now = experiment;
    pthread_mutex_unlock(&experiment_lock);
    return now;
}

int ps_capture_hevc_allowed(void)
{
    const ps_experiment now = experiment_now();
    return ps_capture_hevc_verified() || ps_exp_hevc_offered(&now);
}

int ps_capture_hdr_allowed(void)
{
    const ps_experiment now = experiment_now();
    return ps_capture_hevc_verified() || ps_exp_hdr_offered(&now);
}

int ps_capture_large_modes_allowed(void)
{
    const ps_experiment now = experiment_now();
    return ps_capture_large_modes_verified() || ps_exp_large_offered(&now);
}

int ps_capture_high_bitrate_allowed(void)
{
    const ps_experiment now = experiment_now();
    return ps_capture_high_bitrate_verified() || ps_exp_bitrate_offered(&now);
}

int ps_capture_small_modes_allowed(void)
{
    const ps_experiment now = experiment_now();
    return ps_capture_small_modes_verified() || ps_exp_small_offered(&now);
}

static void mode_snapshot(int slot, uint32_t mode[7])
{
    mode[5] = atomic_load(&video_ten_bit[slot]);
    mode[0] = atomic_load(&video_resolution[slot]);
    mode[1] = atomic_load(&video_timing[slot]);
    mode[2] = atomic_load(&video_codec[slot]);
    mode[3] = atomic_load(&opening_average[slot]);
    mode[4] = atomic_load(&opening_peak[slot]);
    const ps_experiment now = experiment_now();
    mode[6] = (uint32_t)ps_exp_variant_for(&now, mode[2], mode[5]);
}

int ps_capture_mode_current(const ps_capture *s)
{
    if (!s) return 0;
    uint32_t mode[7];
    mode_snapshot(s->slot, mode);
    return !memcmp(mode, s->mode, sizeof mode);
}

int ps_capture_slot_set_video_mode(int slot, uint32_t resolution, uint32_t timing)
{
    // Slot 1 has profile 175, which grants scanin[2K] only.
    if (!valid_slot(slot) || resolution < 1 || resolution > (slot ? 4u : 6u) || timing > 1) return PS_CAPTURE_ERROR;
    atomic_store(&video_resolution[slot], resolution);
    atomic_store(&video_timing[slot], timing);
    // Profile 180 grants scanin[2K], 182 scanin[4K] and more of the encoder (docs/re/avcap2-exports.md).
    atomic_store(&resource_id[slot], slot ? 175 : resolution >= 5 ? 182 : 180);
    return 0;
}

int ps_capture_set_video_mode(uint32_t resolution, uint32_t timing) { return ps_capture_slot_set_video_mode(0, resolution, timing); }

uint32_t ps_capture_slot_video_resolution(int slot) { return valid_slot(slot) ? atomic_load(&video_resolution[slot]) : 0; }

uint32_t ps_capture_video_resolution(void) { return ps_capture_slot_video_resolution(0); }

int ps_capture_slot_fall_back_mode(int slot)
{
    if (!valid_slot(slot) || !atomic_load(&refused[slot]) || !ps_exp_mode_may_fall_back(atomic_load(&video_resolution[slot]))) return 0;
    ps_capture_slot_set_video_mode(slot, 4, atomic_load(&video_timing[slot]));
    return 1;
}

int ps_capture_fall_back_mode(void) { return ps_capture_slot_fall_back_mode(0); }

static _Atomic size_t override_field;
static _Atomic uint32_t override_value;

void ps_capture_set_resource(int id) { atomic_store(&resource_id[0], id); }

void ps_capture_override_config(size_t field, uint32_t value)
{
    atomic_store(&override_value, value);
    atomic_store(&override_field, field);
}

static _Atomic size_t extra_field[3];
static _Atomic uint32_t extra_value[3];

void ps_capture_override_extra(int slot, size_t field, uint32_t value)
{
    if (slot < 0 || slot > 2) return;
    atomic_store(&extra_value[slot], value);
    atomic_store(&extra_field[slot], field);
}

// Research hooks (capture.h). Set from the probe's one thread before it opens, so plain statics will do.
static ps_capture_record_hook record_hook;
static void *record_context;
static _Atomic int open_only, open_attempts;

void ps_capture_set_record_hook(ps_capture_record_hook hook, void *context)
{
    record_hook = hook;
    record_context = context;
}

void ps_capture_set_open_only(int value) { atomic_store(&open_only, value != 0); }

void ps_capture_set_open_attempts(int attempts) { atomic_store(&open_attempts, attempts > 0 ? attempts : 0); }

// Firmware 13.60: the block at 0xb8 is 8 bytes larger, and what 13.00 keeps from 0x198 on sits 8 higher. Not measured
// here, taken from two public projects that run on it (docs/firmware-porting.md). 13.40 refused 13.00's record at OpenVideo
// with 0x80c70010, as 13.60 did before it got this one (GitHub issues 14 and 1), so it gets 13.60's, and so does
// anything between them. 12.70 streams with 13.00's (issue 17); where between 13.00 and 13.40 the block grew is unknown.
// Firmware 10.x (measured on 10.20): the block is 0x18 bytes smaller, and what 13.00 keeps from 0x198 on sits 0x18 lower.
// 13.00's rate fields land on other encoder fields there (Start returns 0x80c78000). The measured record also leaves out
// 13.00's inner sub-block headers, so 10.x writes neither, and the encoder sets no rate. Its QP follows the client's
// bitrate there instead (encoder_qp.h).
// Firmware 9.60 (firmware probe): the block is 0x20 bytes smaller, and only that size opens from -0x40 to +0x20. Like 10.x
// it takes neither inner headers nor rate fields, and its QP sits 0x18 lower (encoder_qp.h). Anything else below 10 gets
// 9.60's record too: 8.40 refused 13.00's at OpenVideo with 0x81950008 (GitHub issue 23). Not measured there.
// HEVC is offered on 13.00 and 10.20 (ps_capture_hevc_verified). On 10.20 cfg+0x24 alone starts it, 8 and 10 bit, so
// Remote Play's fields and the encoder memory are left out on 10.x. On 13.60 its fields would stay at 13.00's offsets;
// PS_EXP_FULL moves those from 0x198 on with the rest. Where the inner ones sit there is unknown.
static ptrdiff_t record_shift(void)
{
    const uint32_t firmware = kernel_get_fw_version();
    if (firmware < 0x10000000u) return -0x20;
    if (ps_exp_short_record(firmware)) return -0x18;
    return firmware >= 0x13400000u && firmware < 0x14000000u ? 8 : 0;
}

ptrdiff_t ps_capture_record_shift(void) { return record_shift(); }

void ps_capture_build_config(uint8_t *p, const ps_capture_video_config *c, uint32_t mode, int variant)
{
    static const struct { size_t at; uint64_t size; } headers[] = {
        {0, 0x2e0}, {8, 0x28}, {0x30, 0x58}, {0x38, 0x18},
        {0x50, 0x10}, {0x88, 0x30}, {0xb8, 0xe0}, {0xc0, 0x20},
        {0xe0, 0x20}, {0x100, 0x20}, {0x120, 0x18}, {0x1e8, 0xf8},
        {0x1f0, 0x28}, {0x218, 0x88}, {0x2a8, 0x10}
    };
    const ptrdiff_t shift = record_shift();
    memset(p, 0, (size_t)(0x2e0 + shift));
    for (size_t i = 0; i < sizeof headers / sizeof headers[0]; i++) {
        const size_t at = headers[i].at;
        if (shift < 0 && at > 0xb8 && at < 0x198) continue;
        u64(p, at >= 0x198 ? at + shift : at, at == 0 || at == 0xb8 ? headers[i].size + shift : headers[i].size);
    }
    u32(p, 0x10, 0);
    // Purpose: Remote Play. HEVC above 1080p with it needs the service's memory pools, which are missing (docs/re/hevc.md);
    // video chat has the same encoder preset and allocates per request.
    u32(p, 0x14, c->codec == 1 && c->resolution >= 5 ? 6 : 2);
    u32(p, 0x18, c->resolution);
    u32(p, 0x1c, c->timing);
    u32(p, 0x20, c->ten_bit);
    u32(p, 0x24, c->codec);
    u32(p, 0x28, (uint32_t)c->resource);
    const uint64_t peak = c->peak, fps = c->timing ? 60 : 30;
    // Frames between periodic IDRs. Clients ask for one after a loss, so these are only a safety net, and each one
    // costs bits and, with HEVC, skipped frames (docs/re/hevc.md).
    u32(p, 0x94, (uint32_t)(fps * 10));
    if (shift >= 0) {
        // Rate fields as SceRemotePlay 0x70130 computes them: average, peak, bits per frame.
        u32(p, 0xc8, c->average);
        u32(p, 0xcc, (uint32_t)peak);
        u32(p, 0xd0, (uint32_t)(peak * 1001 / (fps * 1000)));
    } else {
        // No rate fields below 11: the encoder's QP follows the client's bitrate instead (encoder_qp.h).
        const uint32_t average = c->average;
        const ps_qp qp = ps_qp_for_bitrate(average);
        u32(p, ps_qp_offset(shift), qp.idr);
        u32(p, ps_qp_offset(shift) + 4, qp.other);
        LOG("firmware 10.x: no rate fields, QP %u/%u for %u kbit/s\n", qp.idr, qp.other, average / 1000);
    }
    if (shift < 0 && c->codec == 0 && !c->no_slices) {
        // 10.x cuts H.264 into one slice per macroblock row, 34 at 1080p, and the hardware decoders on Windows take at most
        // 32. Remote Play sets these three for H.264 (SceRemotePlay 0x6f9df..0x6fa32): on, slices per frame, on. 13.00
        // sends one slice per frame anyway (STATUS.md), so they are left out there. 10.20 accepts them and still cuts 34
        // (measured by a contributor on 0.3.1), so on 10.x the count must sit elsewhere; these offsets are 13.00's.
        u32(p, 0x40, 1);
        u32(p, 0x44, PS_H264_SLICES);
        u32(p, 0x48, 1);
    }
    u32(p, 0x2a0 + shift, mode);
    u32(p, 0x2a4 + shift, 2); // composited screen, including home menu
    u32(p, 0x2b0 + shift, 1);
    p[0x2b4 + shift] = 1;
    if (shift >= 0 && c->codec == 1 && variant != PS_EXP_MINIMAL) {
        const ptrdiff_t high = variant == PS_EXP_FULL ? shift : 0;
        // Remote Play's values (SceRemotePlay 0x6f7f0). With only the fields it sets for HEVC alone,
        // sceAvcap2Start returns 0x80c78000. The set is not minimised.
        u32(p, 0x44, 1);
        u32(p, 0x48, 1);
        p[0x58] = 1;
        u32(p, 0x5c, 9);
        u64(p, 0x60, 0x18);
        p[0x68] = 1;
        u32(p, 0x98, 1);
        u32(p, 0xd4, 35);
        u32(p, 0xd8, 0x33);
        p[0x108] = 1;
        // Bytes a frame may cost before the encoder skips frames to catch up. Remote Play's one frame's share of the peak
        // lost a frame per IDR. These are the largest values measured to start; above them, Start returns 0x80c78000
        // (docs/re/hevc.md).
        static const uint32_t budgets[] = {0, 0x20000, 0x20000, 0x40000, 0x60000, 0xc0000, 0x100000};
        u32(p, 0x10c, budgets[c->resolution < 7 ? c->resolution : 0]);
        p[0x128] = 1;
        u32(p, 0x12c, 0x19);
        u64(p, 0x188, 0x10);
        p[0x190] = 1;
        u32(p, 0x194, 1);
        memset(p + 0x220 + high, 0xff, 0x80);
        u64(p, 0x2b8 + high, 0x10);
        u32(p, 0x2c0 + high, 1);
        u32(p, 0x2c4 + high, 0x6e);
    }
}

// Set when the firmware refused an open with the slice fields; the host then opens without them until it restarts.
static _Atomic int slices_refused;

static _Atomic int retry_without_slices[PS_CAPTURE_SLOTS];

static int uses_slice_fields(int slot)
{
    return record_shift() < 0 && atomic_load(&video_codec[slot]) == 0 && !atomic_load(&slices_refused);
}

static void configure(int slot, uint8_t *p, uint32_t mode, int variant)
{
    const ps_capture_video_config c = {
        .resolution = atomic_load(&video_resolution[slot]),
        .timing = atomic_load(&video_timing[slot]),
        .codec = atomic_load(&video_codec[slot]),
        .ten_bit = atomic_load(&video_ten_bit[slot]),
        .average = atomic_load(&opening_average[slot]),
        .peak = atomic_load(&opening_peak[slot]),
        .resource = atomic_load(&session_resource[slot]),
        .no_slices = atomic_load(&slices_refused),
    };
    ps_capture_build_config(p, &c, mode, variant);
    if (atomic_load(&override_field)) u32(p, atomic_load(&override_field), atomic_load(&override_value));
    for (int i = 0; i < 3; i++)
        if (atomic_load(&extra_field[i])) u32(p, atomic_load(&extra_field[i]), atomic_load(&extra_value[i]));
    if (record_hook) record_hook(p, PS_CAPTURE_RECORD_CAPACITY, record_shift(), record_context);
}

int ps_capture_close(ps_capture *s)
{
    if (!s) return 0;
    const int slot = s->slot;
    int failed = 0, rc;
#define CLEANUP(flag, expression, name) do { \
    if (flag) { rc = (expression); SLOG(slot, name " rc=%#x\n", rc); if (rc < 0) failed = 1; } \
} while (0)
    if (ps_capture_audio_close(s)) failed = 1;
    CLEANUP(s->started, s->stop(s->handle), "Stop");
    CLEANUP(s->opened, s->close(s->handle), "Close");
    CLEANUP(s->has_memory, s->free_memory(s->memory, s->memory + 0x20), "free encoder memory");
    if (s->initialized) {
        pthread_mutex_lock(&init_lock);
        if (--initialized_sessions == 0) CLEANUP(1, s->terminate(), "Terminate");
        else SLOG(slot, "Terminate left to the other channel\n");
        pthread_mutex_unlock(&init_lock);
    }
    CLEANUP(s->requested, s->request(s->resource, "STOP", NULL, getpid()), "STOP");
    int unregistered = 1;
    if (s->registered) {
        rc = s->unregister(s->resource);
        SLOG(slot, "unregister rc=%#x\n", rc);
        if (rc) { failed = 1; unregistered = 0; }
    }
#undef CLEANUP
    free(s);
    // If unregister failed, keep the slot's guard closed: do not start a new
    // session while an old callback registration might still exist.
    if (unregistered && !failed) atomic_flag_clear(&owned[slot]);
    return failed ? PS_CAPTURE_ERROR : 0;
}

// For the attempt's outcome line: where the slot's last open stopped, its raw code, and whether it got as far as the encoder.
static const char *_Atomic failure_step[PS_CAPTURE_SLOTS] = {"", ""};
static _Atomic int failure_rc[PS_CAPTURE_SLOTS], reached_encoder[PS_CAPTURE_SLOTS];
static _Atomic int check_first_frame[PS_CAPTURE_SLOTS];

// Slot 0, where ps_capture_open opens.
int ps_capture_last_failure(const char **step)
{
    if (step) *step = atomic_load(&failure_step[0]);
    return atomic_load(&failure_rc[0]);
}

// How long a first frame may take before an unproven open counts as failed. A frame takes 17 ms at 60 fps.
#ifndef PS_FIRST_FRAME_MS
#define PS_FIRST_FRAME_MS 3000
#endif

// Headers and a frame, as the stream will read them. The frame is lost, which costs nothing: the stream asks for an IDR
// when it starts. Returns 0 or the reader's error.
static int first_frame(ps_capture *s, const char **step)
{
    enum { capacity = 4 * 1024 * 1024 };
    uint8_t *buffer = malloc(capacity);
    if (!buffer) return 0;
    uint8_t descriptor[0x88];
    *step = "headers";
    int rc = ps_capture_headers(s, buffer, capacity);
    if (rc > 0) {
        rc = 0;
        *step = "first frame";
        int waited = 0;
        while (!rc && waited < PS_FIRST_FRAME_MS) {
            rc = s->mode[2] == 1 ? ps_capture_read2(s, buffer, capacity, descriptor) : ps_capture_read(s, buffer, capacity, descriptor);
            if (!rc) {
                usleep(2000);
                waited += 2;
            }
        }
        SLOG(s->slot, "first frame rc=%#x after %d ms\n", rc, waited);
        if (rc > 0) {
            const int slices = ps_nal_slices(buffer, (size_t)rc, s->mode[2] == 1);
            SLOG(s->slot, "first frame has %d slices%s\n", slices,
                 s->mode[2] != 1 && slices > PS_DECODER_MAX_SLICES ? ", more than hardware decoders on Windows take" : "");
        }
        rc = rc > 0 ? 0 : rc ? rc : PS_CAPTURE_ERROR;
    } else if (!rc) {
        rc = PS_CAPTURE_ERROR;
    }
    free(buffer);
    return rc;
}

// The resources the arbitrator lists as active, for the log. Returns list()'s result.
static int active_resources(int (*list)(int32_t *), char *out, size_t size, char separator)
{
    int32_t now[32] = {0};
    const int rc = list(now);
    const char between[2] = {separator, 0};
    size_t used = 0;
    out[0] = 0;
    for (size_t i = 0; i < 32; i++)
        if (now[i]) used += (size_t)snprintf(out + used, size - used, "%s%d", used ? between : "", now[i]);
    return rc;
}

// Caller holds open_lock.
static int open_slot(int slot, ps_capture **out)
{
    int slice_fields = 0;
    atomic_store(&failure_rc[slot], 0);
    atomic_store(&reached_encoder[slot], 0);
    atomic_store(&unsettled[slot], 0);
    atomic_store(&failure_step[slot], "in use");
    if (atomic_flag_test_and_set(&owned[slot])) return PS_CAPTURE_BUSY;
    ps_capture *s = calloc(1, sizeof *s);
    if (!s) { atomic_flag_clear(&owned[slot]); return PS_CAPTURE_ERROR; }
    s->slot = slot;
    // The app's report tells from "open start", "open ok" and "open failed" how this attempt ended, also
    // when a step takes the process down (no outcome line then).
    SLOG(slot, "open start codec=%u resource=%d\n", atomic_load(&video_codec[slot]), atomic_load(&resource_id[slot]));
    s->resource = atomic_load(&resource_id[slot]);
    atomic_store(&session_resource[slot], s->resource);
    const char *step = "system modules";
    atomic_store(&granted[slot], 0);
    atomic_store(&revoked[slot], 0);
    atomic_store(&refused[slot], 0);
    atomic_store(&refused_value[slot], -1);
    int error = PS_CAPTURE_ERROR;
    load_fn load = NULL;
    const char *kernels[] = {"libkernel_web.sprx", "libkernel_sys.sprx", "libkernel.sprx"};
    for (size_t i = 0; i < sizeof kernels / sizeof kernels[0] && !load; i++) {
        uint32_t h = 0;
        if (!kernel_dynlib_handle(-1, kernels[i], &h) && h)
            load = (load_fn)kernel_dynlib_dlsym(-1, h, "sceKernelLoadStartModule");
    }
    if (!load || !module(load, "libSceIpmi.sprx")) goto fail;
    uint32_t av = module(load, "libSceAvcap2.sprx");
    uint32_t ra = module(load, "libSceResourceArbitrator.sprx");
    if (!av || !ra) goto fail;
    // Firmware gate: the four ResourceArbitrator calls are exported only under NIDs whose names are
    // unknown, so they are resolved by NID and checked against the offsets verified per firmware
    // (ra_resolve.c, docs/firmware-porting.md). Resolved once per process (opens are serialised by
    // open_lock); a refusal is final.
    static ps_ra_functions ra_calls;
    static int ra_state; // 0 not yet resolved, 1 resolved, -1 refused
    if (!ra_state) ra_state = ps_ra_resolve(ra, &ra_calls) ? -1 : 1;
    step = "ResourceArbitrator";
    if (ra_state < 0) { error = PS_CAPTURE_FIRMWARE; goto fail; }
    intptr_t base = kernel_dynlib_mapbase_addr(-1, ra);
    if (!base) goto fail;
    int (*reg)(int, void *, void *) = (void *)(base + ra_calls.offset[PS_RA_REGISTER]);
    s->unregister = (void *)(base + ra_calls.offset[PS_RA_UNREGISTER]);
    s->request = (void *)(base + ra_calls.offset[PS_RA_REQUEST]);
    int (*list)(int32_t *) = (void *)(base + ra_calls.offset[PS_RA_LIST]);
    int32_t active[32] = {0};
    step = "resource list";
    // Right after our own close the arbitrator can list the resource for a moment longer: a reopen failed on that.
    // Stay well below the 2 s a client waits for the stream.
    for (int attempt = 0;; attempt++) {
        if (list(active)) goto fail;
        int listed = 0;
        for (size_t i = 0; i < 32; i++) if (active[i] == s->resource) listed = 1;
        if (!listed) {
            if (attempt) SLOG(slot, "resource free after %d ms\n", attempt * 20);
            break;
        }
        if (attempt == 40) { error = PS_CAPTURE_BUSY; goto fail; }
        usleep(20000);
    }

    step = "Avcap2 functions";
#define SYM(name) kernel_dynlib_dlsym(-1, av, name)
    int (*init)(void) = (void *)SYM("sceAvcap2Initialize");
    int (*mode)(int *) = (void *)SYM("sceAvcap2GetVideoOutMode");
    int (*open_video)(intptr_t *, const void *, uint8_t) = (void *)SYM("sceAvcap2OpenVideo");
    handle_fn start = s->start = (void *)SYM("sceAvcap2Start");
    s->terminate = (void *)SYM("sceAvcap2Terminate");
    s->stop = (void *)SYM("sceAvcap2Stop");
    s->close = (void *)SYM("sceAvcap2Close");
    s->headers = (void *)SYM("sceAvcap2GetNonVclNalUnitStream");
    s->read = (void *)SYM("sceAvcap2ReadVideo");
    s->read2 = (void *)SYM("sceAvcap2ReadVideo2");
    s->request_keyframe = (void *)SYM("sceAvcap2RequestKeyFrame");
    s->set_bitrate = (void *)SYM("sceAvcap2SetVideoBitrate");
    s->open_audio = (void *)SYM("sceAvcap2OpenAudio");
    s->read_audio = (void *)SYM("sceAvcap2ReadAudio");
#undef SYM
    if (!init || !mode || !open_video || !start || !s->terminate || !s->stop || !s->close || !s->headers || !s->read)
        goto fail;
    step = "register";
    int rc = reg(s->resource, (void *)resource_event, NULL);
    SLOG(slot, "register rc=%#x\n", rc);
    if (rc) goto fail;
    s->registered = 1;
    s->requested = 1; // STOP is attempted even if the RUNNING request reports failure.
    step = "RUNNING";
    rc = s->request(s->resource, "RUNNING", NULL, getpid());
    SLOG(slot, "RUNNING rc=%#x\n", rc);
    if (rc < 0) goto fail;
    // The open runs once the client's first ping came, and Moonlight waits 10 s for the first video packet
    // (FIRST_FRAME_TIMEOUT_SEC, moonlight-common-c). On 13.60 the grant once took longer than 2 s twice in a row, and the
    // same request went through right away ten seconds later (GitHub issue 1).
    int waited_ms = 0;
    for (; waited_ms < 6000 && !atomic_load(&granted[slot]) && !atomic_load(&revoked[slot]) && !atomic_load(&refused[slot]); waited_ms += 10)
        usleep(10000);
    if (waited_ms >= 500 && atomic_load(&granted[slot]) && !atomic_load(&revoked[slot])) SLOG(slot, "RUNNING granted after %d ms\n", waited_ms);
    if (!atomic_load(&granted[slot]) || atomic_load(&revoked[slot])) {
        // The app's report reads this line (parse_host_log): no answer, refused or taken away are different problems.
        char ids[32 * 12];
        const int list_rc = active_resources(list, ids, sizeof ids, ',');
        SLOG(slot, "RUNNING outcome=%s value=%d waited_ms=%d list_rc=%#x active=%s\n",
             atomic_load(&revoked[slot]) ? "revoked" : atomic_load(&refused[slot]) ? "refused" : "timeout", atomic_load(&refused_value[slot]),
             waited_ms, list_rc, ids[0] ? ids : "none");
        goto fail;
    }
    step = "Initialize";
    pthread_mutex_lock(&init_lock);
    if (initialized_sessions) {
        rc = 0;
        SLOG(slot, "Initialize done by the other channel\n");
    } else {
        rc = init();
        SLOG(slot, "Initialize rc=%#x\n", rc);
    }
    if (!rc) {
        initialized_sessions++;
        s->initialized = 1;
    }
    pthread_mutex_unlock(&init_lock);
    if (rc) goto fail;
    step = "video out mode";
    int output = 0;
    // While a title comes up the output has no mode (0) for about a second, and the service refuses a config whose mode isn't
    // the current one. Stay below the 2 s a client waits for the stream.
    for (int attempt = 0; attempt < 50; attempt++) {
        if (mode(&output)) goto fail;
        if (output) {
            if (attempt) SLOG(slot, "video out mode appeared after %d ms\n", attempt * 20);
            break;
        }
        usleep(20000);
    }
    SLOG(slot, "video out mode %#x\n", output);
    if (output & 3) { error = PS_CAPTURE_VR; goto fail; }
    atomic_store(&reached_encoder[slot], 1);
    uint8_t config[PS_CAPTURE_RECORD_CAPACITY] = {0};
    mode_snapshot(slot, s->mode);
    slice_fields = uses_slice_fields(slot);
    configure(slot, config, (uint32_t)output, (int)s->mode[6]);
    if (atomic_load(&video_codec[slot]) == 1 && record_shift() >= 0 && s->mode[6] != PS_EXP_MINIMAL) {
        // Memory Mode 1 as Remote Play sets it up (SceRemotePlay 0x6fb83..0x6fc65).
        step = "encoder memory";
        const ptrdiff_t high = s->mode[6] == PS_EXP_FULL ? record_shift() : 0;
        int (*reserve)(const void *, void *, void *) = (void *)kernel_dynlib_resolve(-1, av, "yN0w4tWivqs");
        s->free_memory = (void *)kernel_dynlib_resolve(-1, av, "96UQhNnBlxc");
        if (!reserve || !s->free_memory) goto fail;
        uint8_t request[0x90] = {0};
        u64(request, 0, sizeof request);
        memcpy(request + 8, config + 8, 0x28);
        memcpy(request + 0x30, config + 0x30, 0x58);
        u64(config, 0x198 + high, 0x50);
        u32(config, 0x1a0 + high, 1);
        rc = reserve(request, config + 0x1a8 + high, config + 0x1c8 + high);
        SLOG(slot, "encoder memory rc=%#x\n", rc);
        if (rc) { atomic_store(&last_open_rc, rc); atomic_store(&failure_rc[slot], rc); goto fail; }
        memcpy(s->memory, config + 0x1a8 + high, sizeof s->memory);
        s->has_memory = 1;
    }
    step = "OpenVideo";
    // The service refuses while it doesn't list our resource as active. Seen on firmware 10.40, which may just need a
    // moment; stay below the 2 s a client waits for the stream (Initial Ping Timeout). A larger mode gets the same code
    // when the console's output is too small for it, and needs the time left for the open at 1080p, as does a smaller one.
    // It also answers this when the output's mode changed since it was read, as while a title comes up, so that is read anew.
    const int repeats = atomic_load(&open_attempts) ? atomic_load(&open_attempts) - 1
                        : atomic_load(&video_resolution[slot]) != 4 ? 5 : 25;
    int changed = 0;
    for (int attempt = 0;; attempt++) {
        rc = open_video(&s->handle, config, 0);
        if (rc != RESOURCE_NOT_ACTIVE || attempt == repeats || atomic_load(&revoked[slot])) {
            if (attempt) SLOG(slot, "OpenVideo repeated %d times\n", attempt);
            break;
        }
        usleep(40000);
        const int before = output;
        if (mode(&output) == 0 && output != before) {
            SLOG(slot, "video out mode now %#x\n", output);
            changed = 1;
            u32(config, 0x2a0 + record_shift(), (uint32_t)output);
        }
    }
    atomic_store(&unsettled[slot], rc && (changed || !output));
    atomic_store(&last_open_rc, rc);
    atomic_store(&failure_rc[slot], rc);
    SLOG(slot, "OpenVideo rc=%#x\n", rc);
    if (rc) atomic_store(&refused[slot], 1);
    if (rc == RESOURCE_NOT_ACTIVE) {
        // For the app's report: is our resource listed, and which others are?
        char ids[32 * 12];
        const int list_rc = active_resources(list, ids, sizeof ids, ' ');
        SLOG(slot, "active resources rc=%#x:%s%s\n", list_rc, ids[0] ? " " : "", ids);
    }
    if (rc) goto fail;
    s->opened = 1;
    if (atomic_load(&open_only)) {
        step = "open only";
        goto fail;
    }
    step = "Start";
    rc = start(s->handle);
    SLOG(slot, "Start rc=%#x\n", rc);
    atomic_store(&failure_rc[slot], rc);
    if (rc) { atomic_store(&refused[slot], 1); goto fail; }
    s->started = 1;
    if (!atomic_load(&granted[slot]) || atomic_load(&revoked[slot])) { error = PS_CAPTURE_REVOKED; goto fail; }
    // With the slice fields, a frame must come before the stream counts on them.
    if (atomic_load(&check_first_frame[slot]) || slice_fields) {
        rc = first_frame(s, &step);
        if (rc == PS_CAPTURE_REVOKED) { error = PS_CAPTURE_REVOKED; goto fail; }
        if (rc) { atomic_store(&refused[slot], 1); goto fail; }
    }
    *out = s;
    SLOG(slot, "open ok\n");
    return 0;
fail:
    if (slice_fields && error != PS_CAPTURE_REVOKED && atomic_load(&reached_encoder[slot]) && !atomic_load(&open_only) &&
        !(strcmp(step, "OpenVideo") == 0 && atomic_load(&failure_rc[slot]) == RESOURCE_NOT_ACTIVE)) {
        // The firmware may not know the fields: ps_capture_slot_open tries once more without them. Not after the probe's
        // open only, which stops on purpose.
        atomic_store(&retry_without_slices[slot], 1);
    }
    atomic_store(&failure_step[slot], step);
    SLOG(slot, "open failed step=%s error=%d rc=%#x\n", step, error, atomic_load(&last_open_rc));
    ps_capture_close(s);
    return error;
}

int ps_capture_slot_open(int slot, ps_capture **out)
{
    if (!out) return PS_CAPTURE_ERROR;
    *out = NULL;
    if (!valid_slot(slot)) return PS_CAPTURE_ERROR;
    pthread_mutex_lock(&open_lock);
    int rc = open_slot(slot, out);
    if (rc && atomic_exchange(&retry_without_slices[slot], 0)) {
        // Without the fields the stream works as before, only with more slices. They stay off only if they were at fault.
        atomic_store(&slices_refused, 1);
        rc = open_slot(slot, out);
        if (rc) atomic_store(&slices_refused, 0);
        SLOG(slot, "H.264 slice fields: %s\n", rc ? "the open failed without them too, kept" : "refused, left out from now on");
    }
    pthread_mutex_unlock(&open_lock);
    return rc;
}

int ps_capture_open(ps_capture **out) { return ps_capture_slot_open(0, out); }

// host_diag.log gets a line for each attempt while a switch counts, and the app's parser reads them
// (parse_host_log in app/src/psapp/diag.cpp), so its field names and this have to agree.
static void names(char *out, size_t size, const uint8_t flags[PS_EXP_SWITCH_COUNT])
{
    size_t used = 0;
    out[0] = 0;
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; i++)
        if (flags[i]) used += (size_t)snprintf(out + used, size - used, "%s%s", used ? "," : "", ps_exp_switch_at(i)->name);
    if (!used) snprintf(out, size, "none");
}

static void describe_attempt(char *out, size_t size, const ps_experiment *e, uint32_t codec, uint32_t ten_bit, uint32_t resolution,
                             uint32_t timing, uint32_t average)
{
    static const char *const variants[] = {"standard", "minimal", "full"};
    // By ps_capture_set_video_mode's resolution, which it keeps within 1..6.
    static const char *const modes[] = {"", "640x360", "960x540", "1280x720", "1920x1080", "2560x1440", "3840x2160"};
    char on[64], trial[64];
    names(on, sizeof on, e->on);
    const int unproven = ps_exp_unproven_codec(e, codec, ten_bit);
    uint8_t proving[PS_EXP_SWITCH_COUNT] = {0};
    proving[PS_EXP_HEVC] = unproven && !ten_bit;
    proving[PS_EXP_HDR] = unproven && ten_bit;
    const int mode = ps_exp_mode_switch(resolution);
    if (mode >= 0) proving[mode] = (uint8_t)ps_exp_unproven_mode(e, resolution);
    proving[PS_EXP_BITRATE] = (uint8_t)ps_exp_unproven_rate(e, average);
    names(trial, sizeof trial, proving);
    snprintf(out, size, "switches=%s firmware=%#x trial=%s codec=%s bits=%d mode=%s fps=%d kbit=%u variant=%s", on, kernel_get_fw_version(), trial,
             codec == 1 ? "hevc" : "h264", ten_bit ? 10 : 8, modes[resolution], timing ? 60 : 30, average / 1000,
             variants[ps_exp_variant_for(e, codec, ten_bit)]);
}

// A file names the attempt under way: a step that takes the host down leaves no outcome, and without this the next host
// would take the same way. The next start reads it and keeps that function off for its run.
static char marker_path[256];

static void marker_write(const char *fields)
{
    if (!marker_path[0]) return;
    const int fd = open(marker_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    if (write(fd, fields, strlen(fields)) < 0) LOG("experimental attempt not noted\n");
    fsync(fd);
    close(fd);
}

static void marker_clear(void)
{
    if (marker_path[0]) unlink(marker_path);
}

static void log_disabled(const ps_experiment *before, const ps_experiment *after)
{
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; i++)
        if (after->failed[i] && !before->failed[i]) ps_diag_log("ProsperoSunshine: experimental disabled feature=%s\n", ps_exp_switch_at(i)->name);
}

void ps_capture_set_experimental(const int switches[PS_EXP_SWITCH_COUNT], const char *marker)
{
    const uint32_t firmware = kernel_get_fw_version();
    ps_experiment now;
    ps_exp_init(&now, firmware, switches);
    uint8_t asked[PS_EXP_SWITCH_COUNT];
    int any = 0;
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; i++) any |= asked[i] = switches[i] != 0;
    if (any) {
        char set[64], on[64];
        names(set, sizeof set, asked);
        names(on, sizeof on, now.on);
        ps_diag_log("ProsperoSunshine: experimental switches=%s set=%s firmware=%#x\n", on, set, firmware);
    }
    snprintf(marker_path, sizeof marker_path, "%s", marker ? marker : "");
    char left[256] = "";
    const int fd = marker_path[0] ? open(marker_path, O_RDONLY) : -1;
    if (fd >= 0) {
        const ssize_t got = read(fd, left, sizeof left - 1);
        left[got > 0 ? got : 0] = 0;
        close(fd);
    }
    const ps_experiment before = now;
    if (left[0]) {
        ps_diag_log("ProsperoSunshine: experimental result=failed %s error=0 rc=0 step=host went down\n", left);
        const char *trial = strstr(left, " trial=");
        if (trial) {
            const char *const end = strchr(trial + 1, ' ');
            const size_t length = end ? (size_t)(end - trial) : strlen(trial);
            char words[80];
            snprintf(words, sizeof words, "%.*s", (int)length, trial);
            for (int i = 0; i < PS_EXP_SWITCH_COUNT; i++) {
                // The HDR switch alone also offers HEVC.
                const int counts = now.on[i] || (i == PS_EXP_HEVC && now.on[PS_EXP_HDR]);
                if (counts && strstr(words, ps_exp_switch_at(i)->name)) now.failed[i] = 1;
            }
        }
    }
    marker_clear();
    log_disabled(&before, &now);
    pthread_mutex_lock(&experiment_lock);
    experiment = now;
    pthread_mutex_unlock(&experiment_lock);
}

// One attempt at a time, for the marker file. Caller holds attempt_lock.
static int open_or_fall_back(int slot, ps_capture **out)
{
    for (;;) {
        const uint32_t codec = atomic_load(&video_codec[slot]), ten_bit = atomic_load(&video_ten_bit[slot]);
        const uint32_t resolution = atomic_load(&video_resolution[slot]), timing = atomic_load(&video_timing[slot]);
        const uint32_t average = atomic_load(&opening_average[slot]);
        const ps_experiment before = experiment_now();
        const int logged = ps_exp_any(&before);
        const int unproven = ps_exp_unproven_codec(&before, codec, ten_bit) || ps_exp_unproven_mode(&before, resolution) ||
                             ps_exp_unproven_rate(&before, average);
        char fields[192] = "";
        if (logged) {
            describe_attempt(fields, sizeof fields, &before, codec, ten_bit, resolution, timing, average);
            ps_diag_log("ProsperoSunshine: experimental attempt %s\n", fields);
            if (unproven) marker_write(fields);
        }
        atomic_store(&check_first_frame[slot], unproven);
        const int rc = ps_capture_slot_open(slot, out);
        atomic_store(&check_first_frame[slot], 0);
        if (logged) {
            if (rc)
                ps_diag_log("ProsperoSunshine: experimental result=failed %s error=%d rc=%#x step=%s\n", fields, rc,
                            (unsigned)atomic_load(&failure_rc[slot]), atomic_load(&failure_step[slot]));
            else
                ps_diag_log("ProsperoSunshine: experimental result=ok %s error=0 rc=0 step=none\n", fields);
            if (unproven) marker_clear();
        }
        pthread_mutex_lock(&experiment_lock);
        enum ps_exp_next next = PS_EXP_GIVE_UP;
        if (rc) {
            const int attributable = rc == PS_CAPTURE_ERROR && atomic_load(&reached_encoder[slot]);
            next = ps_exp_failed(&experiment, codec, ten_bit, resolution, average, attributable,
                                 atomic_load(&refused[slot]) && ps_exp_mode_may_fall_back(resolution));
        } else {
            ps_exp_succeeded(&experiment, codec, ten_bit, resolution, average);
        }
        const ps_experiment after = experiment;
        pthread_mutex_unlock(&experiment_lock);
        log_disabled(&before, &after);
        if (!rc) return 0;
        if (next == PS_EXP_NEXT_VARIANT) continue;
        if (next == PS_EXP_LOWER_RATE) {
            // The peak keeps its share of the average. The client needs no word of this: it gets fewer bits, nothing else.
            const uint64_t peak = atomic_load(&opening_peak[slot]);
            atomic_store(&opening_peak[slot], (uint32_t)(peak * PS_EXP_SAFE_AVERAGE / average));
            atomic_store(&opening_average[slot], PS_EXP_SAFE_AVERAGE);
            continue;
        }
        // A title starting or closing leaves the output without a mode for up to about 2 s (10.20), longer than the opens
        // above wait. A refusal then says nothing about a mode below 1080p, which the client may need: a reopen in a
        // running stream fails instead of falling back, and its loop tries the same mode again.
        if (next == PS_EXP_FALL_BACK && atomic_load(&reopening[slot]) && atomic_load(&unsettled[slot]) && resolution < 4) {
            SLOG(slot, "output not settled: resolution %u kept for the next reopen\n", resolution);
            return rc;
        }
        if (next == PS_EXP_FALL_BACK && ps_capture_slot_fall_back_mode(slot)) continue;
        // Audio may open next, for the stream that is about to end: not with what just failed.
        if (after.failed[PS_EXP_HEVC] != before.failed[PS_EXP_HEVC] || after.failed[PS_EXP_HDR] != before.failed[PS_EXP_HDR]) {
            atomic_store(&video_ten_bit[slot], 0);
            if (!ps_capture_hevc_allowed()) atomic_store(&video_codec[slot], 0);
        }
        return rc;
    }
}

static pthread_mutex_t attempt_lock = PTHREAD_MUTEX_INITIALIZER;

int ps_capture_slot_open_or_fall_back(int slot, ps_capture **out)
{
    if (!out) return PS_CAPTURE_ERROR;
    *out = NULL;
    if (!valid_slot(slot)) return PS_CAPTURE_ERROR;
    pthread_mutex_lock(&attempt_lock);
    const int rc = open_or_fall_back(slot, out);
    pthread_mutex_unlock(&attempt_lock);
    return rc;
}

int ps_capture_open_or_fall_back(ps_capture **out) { return ps_capture_slot_open_or_fall_back(0, out); }

void ps_capture_slot_set_reopening(int slot, int on)
{
    if (valid_slot(slot)) atomic_store(&reopening[slot], on != 0);
}

void ps_capture_set_reopening(int on) { ps_capture_slot_set_reopening(0, on); }

int ps_capture_change_scan_in(ps_capture *s, uint32_t resolution)
{
    if (!s || !s->started) return PS_CAPTURE_ERROR;
    uint32_t h = 0;
    if (kernel_dynlib_handle(-1, "libSceAvcap2.sprx", &h) || !h) return PS_CAPTURE_ERROR;
    int (*change)(intptr_t, uint32_t) = (void *)kernel_dynlib_dlsym(-1, h, "sceAvcap2ChangeScanInResolution");
    return change ? change(s->handle, resolution) : PS_CAPTURE_ERROR;
}

int ps_capture_headers(ps_capture *s, void *destination, size_t capacity)
{
    if (!s || !destination || !s->started) return PS_CAPTURE_ERROR;
    if (!atomic_load(&granted[s->slot]) || atomic_load(&revoked[s->slot])) return PS_CAPTURE_REVOKED;
    uint8_t nal[0x1a8] = {0};
    u64(nal, 0, sizeof nal);
    int rc = s->headers(s->handle, nal);
    if (rc) { atomic_store(&failure_rc[s->slot], rc); SLOG(s->slot, "headers rc=%#x\n", rc); return PS_CAPTURE_ERROR; }
    size_t used = 0;
    // The console lists HEVC's as SPS, PPS, VPS; decoders and moonlight-common-c want the VPS first.
    static const size_t order[][3] = {{0x10, 0x98, 0x120}, {0x120, 0x10, 0x98}};
    for (size_t i = 0; i < 3; i++) {
        const size_t off = order[s->mode[2] == 1][i];
        uint64_t n;
        memcpy(&n, nal + off, sizeof n);
        if (n > 0x80 || (n && (capacity < used || capacity - used < n + 4))) return PS_CAPTURE_ERROR;
        if (!n) continue;
        memcpy((uint8_t *)destination + used, "\0\0\0\1", 4);
        memcpy((uint8_t *)destination + used + 4, nal + off + 8, (size_t)n);
        used += (size_t)n + 4;
    }
    return (int)used;
}

int ps_capture_read(ps_capture *s, void *destination, size_t capacity, uint8_t descriptor[0x70])
{
    if (!s || !destination || !capacity || !descriptor || !s->started) return PS_CAPTURE_ERROR;
    if (!atomic_load(&granted[s->slot]) || atomic_load(&revoked[s->slot])) return PS_CAPTURE_REVOKED;
    memset(descriptor, 0, 0x70);
    u64(descriptor, 0, 0x70);
    int rc = s->read(s->handle, destination, capacity, descriptor, 1);
    if (rc == AGAIN || rc == 0) return 0;
    if (rc < 0 || (size_t)rc > capacity) { atomic_store(&failure_rc[s->slot], rc); SLOG(s->slot, "ReadVideo rc=%#x\n", rc); return PS_CAPTURE_ERROR; }
    return rc;
}

int ps_capture_read2(ps_capture *s, void *destination, size_t capacity, uint8_t descriptor[0x88])
{
    if (!s || !destination || !capacity || !descriptor || !s->started || !s->read2) return PS_CAPTURE_ERROR;
    if (!atomic_load(&granted[s->slot]) || atomic_load(&revoked[s->slot])) return PS_CAPTURE_REVOKED;
    memset(descriptor, 0, 0x88);
    u64(descriptor, 0, 0x88);
    int rc = s->read2(s->handle, destination, capacity, descriptor, 1);
    if (rc == AGAIN || rc == 0) return 0;
    if (rc < 0 || (size_t)rc > capacity) { atomic_store(&failure_rc[s->slot], rc); SLOG(s->slot, "ReadVideo2 rc=%#x\n", rc); return rc < 0 ? rc : PS_CAPTURE_ERROR; }
    return rc;
}

int ps_capture_request_keyframe(ps_capture *s)
{
    if (!s || !s->started || !s->request_keyframe) return PS_CAPTURE_ERROR;
    int rc = s->request_keyframe(s->handle);
    if (rc < 0) { SLOG(s->slot, "RequestKeyFrame rc=%#x\n", rc); return PS_CAPTURE_ERROR; }
    return 0;
}

int ps_capture_set_bitrate(ps_capture *s, uint32_t average, uint32_t peak)
{
    if (!s || !s->started || !s->set_bitrate) return PS_CAPTURE_ERROR;
    uint8_t request[0x10] = {0};
    u64(request, 0, sizeof request);
    u32(request, 8, average);
    u32(request, 0xc, peak);
    int rc = s->set_bitrate(s->handle, request);
    if (rc < 0) { SLOG(s->slot, "SetVideoBitrate rc=%#x\n", rc); return PS_CAPTURE_ERROR; }
    return 0;
}

int ps_capture_audio_open(ps_capture *s, uint32_t encoder, uint32_t bitrate, uint32_t frame_index)
{
    // cfg+0x1C is the ResourceArbitrator id the server checks against the active list, like
    // OpenVideo's cfg+0x28 (docs/re/avcap2-exports.md, R2-Nachtrag).
    return ps_capture_audio_open_ex(s, encoder, bitrate, frame_index, 2, s ? (uint32_t)s->resource : 0, 0);
}

int ps_capture_audio_open_ex(ps_capture *s, uint32_t encoder, uint32_t bitrate, uint32_t frame_index,
                             uint32_t purpose, uint32_t field_1c, uint8_t flag)
{
    if (!s || !s->started || s->audio_opened || !s->open_audio) return PS_CAPTURE_ERROR;
    uint8_t config[0x38] = {0};
    u64(config, 0, sizeof config);
    u64(config, 8, 0x18);
    u32(config, 0x10, 0);
    u32(config, 0x14, purpose);  // 2 Remoteplay: the full game-audio mix
    u32(config, 0x18, encoder);
    u32(config, 0x1c, field_1c);
    u64(config, 0x20, encoder == 2 ? 0x18 : 0x10);
    u32(config, 0x28, bitrate);
    u32(config, 0x30, frame_index);
    int rc = s->open_audio(&s->audio, config, flag);
    SLOG(s->slot, "OpenAudio encoder=%u purpose=%u 1c=%#x flag=%u rc=%#x\n", encoder, purpose, field_1c, flag, rc);
    if (rc) return rc < 0 ? rc : PS_CAPTURE_ERROR;
    s->audio_opened = 1;
    rc = s->start(s->audio);
    SLOG(s->slot, "audio Start rc=%#x\n", rc);
    if (rc) { ps_capture_audio_close(s); return PS_CAPTURE_ERROR; }
    s->audio_started = 1;
    return 0;
}

int ps_capture_audio_read(ps_capture *s, void *destination, size_t capacity, uint8_t header[0x10])
{
    if (!s || !s->audio_started || !s->read_audio || !destination || !header) return PS_CAPTURE_ERROR;
    if (!atomic_load(&granted[s->slot]) || atomic_load(&revoked[s->slot])) return PS_CAPTURE_REVOKED;
    memset(header, 0, 0x10);
    u64(header, 0, 0x10);
    int rc = s->read_audio(s->audio, destination, capacity, header, 1);
    if (rc == AGAIN || rc == 0) return 0;
    if (rc < 0 || (size_t)rc > capacity) { SLOG(s->slot, "ReadAudio rc=%#x\n", rc); return PS_CAPTURE_ERROR; }
    return rc;
}

int ps_capture_audio_close(ps_capture *s)
{
    if (!s) return 0;
    int failed = 0, rc;
    if (s->audio_started) {
        rc = s->stop(s->audio);
        SLOG(s->slot, "audio Stop rc=%#x\n", rc);
        failed |= rc < 0;
    }
    if (s->audio_opened) {
        rc = s->close(s->audio);
        SLOG(s->slot, "audio Close rc=%#x\n", rc);
        failed |= rc < 0;
    }
    s->audio_started = s->audio_opened = 0;
    s->audio = 0;
    return failed ? PS_CAPTURE_ERROR : 0;
}
