// SPDX-License-Identifier: GPL-3.0-only
#ifndef PROSPERO_CAPTURE_H
#define PROSPERO_CAPTURE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ps_capture ps_capture;
enum ps_capture_error {
    PS_CAPTURE_ERROR = -1,
    PS_CAPTURE_BUSY = -2,
    PS_CAPTURE_FIRMWARE = -3,
    PS_CAPTURE_VR = -4,
    PS_CAPTURE_REVOKED = -5
};

// Mode for the next ps_capture_open. resolution: 1 = 640x360, 2 = 960x540, 3 = 1280x720,
// 4 = 1920x1080 (default), 5 = 2560x1440, 6 = 3840x2160; timing: 0 = 30 fps, 1 = 60 fps (default).
int ps_capture_set_video_mode(uint32_t resolution, uint32_t timing);
// Codec for the next ps_capture_open: 0 = H.264 (default), 1 = HEVC.
int ps_capture_set_codec(uint32_t codec);
// 10 bit for the next ps_capture_open, HEVC only: Main 10, BT.2020, PQ (docs/re/hdr.md).
void ps_capture_set_ten_bit(uint32_t ten_bit);
// Whether HEVC was verified on this firmware: 13.00 and 10.20.
int ps_capture_hevc_verified(void);
// Whether 1440p and 2160p were verified on this firmware.
int ps_capture_large_modes_verified(void);
// Whether opening above 50 Mbit/s was verified on this firmware.
int ps_capture_high_bitrate_verified(void);
// Whether 360p, 540p and 720p were verified on this firmware: 10.20 and 13.00.
int ps_capture_small_modes_verified(void);
// The app's Experimental switches from sunshine.conf, read once when the host starts, one value per entry of
// app/src/psapp/experimental_list.h; a switch counts only where its function isn't verified. `marker` is a file that names
// an attempt under way. A marker left by a host that went down keeps that function off for this run.
void ps_capture_set_experimental(const int *switches, const char *marker);
// What the host may offer now: verified, or switched on and not failed since the host started.
int ps_capture_hevc_allowed(void);
int ps_capture_hdr_allowed(void);
int ps_capture_large_modes_allowed(void);
int ps_capture_high_bitrate_allowed(void);
int ps_capture_small_modes_allowed(void);
// The resolution set for the next open. After the system refused the last open of a mode above 1080p (seen in a title,
// the arbitrator logs "4K NG(4K120)"; other sources report OpenVideo refusing it when the console's output is smaller),
// or of one below it, ps_capture_fall_back_mode sets 1080p and returns 1; otherwise it changes nothing
// (ps_exp_mode_may_fall_back).
uint32_t ps_capture_video_resolution(void);
int ps_capture_fall_back_mode(void);
// Bitrate in the config of the next open, in bit/s. Default 6 Mbit/s and no peak. HEVC needs a peak. Raising
// the bitrate above this at runtime kills the channel (0x80c78000 on the next read).
void ps_capture_set_opening_bitrate(uint32_t average, uint32_t peak);
// The average set for the next open: ps_capture_open_or_fall_back lowers it when a high rate fails.
uint32_t ps_capture_opening_average(void);
// Research, for other firmwares: the resource for slot 0's next open (ps_capture_set_video_mode sets the default for the
// mode, so call this after it), and one more 32-bit config field to write after all others (field 0 = none).
void ps_capture_set_resource(int id);
void ps_capture_override_config(size_t field, uint32_t value);
// Research: up to three more fields written after that one (slot 0..2, field 0 = none), for settings that span fields.
void ps_capture_override_extra(int slot, size_t field, uint32_t value);
// Research (dual_open_probe): the OpenVideo record as ps_capture_open writes it, from explicit values instead of the
// settings for the next open, and without the override field. `record` holds 0x2e8 bytes; `output` is
// sceAvcap2GetVideoOutMode's value; variant is a ps_exp_variant (experimental.h), PS_EXP_STANDARD as the host uses by
// default. ps_capture_record_shift: how far this firmware's record moves the fields from 0x198 on (13.00: 0, 13.40 to
// 13.60: 8, 10.x: -0x18, 9.60: -0x20).
typedef struct {
    uint32_t resolution, timing, codec, ten_bit;  // as ps_capture_set_video_mode, _set_codec, _set_ten_bit
    uint32_t average, peak;                       // bit/s
    int resource;                                 // ResourceArbitrator id, cfg+0x28
    // 10.x, H.264: 1 leaves out the slice fields (cfg+0x40/0x44/0x48), after the firmware refused them once.
    int no_slices;
} ps_capture_video_config;
void ps_capture_build_config(uint8_t *record, const ps_capture_video_config *config, uint32_t output, int variant);
ptrdiff_t ps_capture_record_shift(void);
// Research: raw result of the last sceAvcap2OpenVideo, and sceAvcap2ChangeScanInResolution at runtime.
int ps_capture_last_open_rc(void);
int ps_capture_change_scan_in(ps_capture *session, uint32_t resolution);
// Research, for src/firmware_probe.cpp; the host sets none of these. The record hook runs last in configure() and may
// rewrite the whole record (capacity PS_CAPTURE_RECORD_CAPACITY; shift is record_shift() on this firmware). With open
// only, ps_capture_open closes after OpenVideo succeeds instead of starting, and fails at the step "open only" with rc 0.
// Open attempts sets how often OpenVideo is tried while it answers 0x81950008 (0 = the host's choice).
#define PS_CAPTURE_RECORD_CAPACITY 0x300
typedef void (*ps_capture_record_hook)(uint8_t *record, size_t capacity, ptrdiff_t shift, void *context);
void ps_capture_set_record_hook(ps_capture_record_hook hook, void *context);
void ps_capture_set_open_only(int open_only);
void ps_capture_set_open_attempts(int attempts);
// Research, after ps_capture_open failed: the raw code of the step it failed at, and that step's name.
int ps_capture_last_failure(const char **step);
// One session per slot (slot 0 here, see ps_capture_slot_open). Measured FW 13.00 / AVC / SDR configuration.
// On failure *out is NULL and any acquired resources have had cleanup attempted.
int ps_capture_open(ps_capture **out);
// ps_capture_open, then ps_capture_fall_back_mode and once more, and with a switch on the variants and the first frame
// too. Each attempt goes to host_diag.log. After a failure ps_capture_hevc_allowed and the others may say no.
int ps_capture_open_or_fall_back(ps_capture **out);
// Whether the next opens reopen a running stream's capture. Then a refusal of a mode below 1080p while the output had no
// mode or changed it, as while a title starts or closes, fails instead of falling back to 1080p.
void ps_capture_set_reopening(int on);
// Whether the session was opened with the mode set now for its slot. NULL counts as not current.
int ps_capture_mode_current(const ps_capture *session);

// Two channels at once, each with its own mode (STATUS.md, dual_open_probe). Slot 0 is the one the calls above set and
// open: ResourceArbitrator profile 180, 182 above 1080p. Slot 1 is a second channel next to it on profile 175, which grants
// up to 1080p: its ps_capture_slot_set_video_mode refuses larger modes. Each slot has its own settings for the next open,
// at most one session, and its own answers from the arbitrator. Avcap2 is initialized with the first session and
// terminated with the last. Opens run one at a time. These do what the calls above do, for the given slot.
enum { PS_CAPTURE_SLOTS = 2 };
int ps_capture_slot_set_video_mode(int slot, uint32_t resolution, uint32_t timing);
int ps_capture_slot_set_codec(int slot, uint32_t codec);
void ps_capture_slot_set_ten_bit(int slot, uint32_t ten_bit);
void ps_capture_slot_set_opening_bitrate(int slot, uint32_t average, uint32_t peak);
uint32_t ps_capture_slot_opening_average(int slot);
uint32_t ps_capture_slot_video_resolution(int slot);
int ps_capture_slot_fall_back_mode(int slot);
void ps_capture_slot_set_reopening(int slot, int on);
int ps_capture_slot_open(int slot, ps_capture **out);
int ps_capture_slot_open_or_fall_back(int slot, ps_capture **out);
// Annex-B parameter sets, for HEVC in the order VPS, SPS, PPS; returns bytes written or a negative error.
int ps_capture_headers(ps_capture *session, void *destination, size_t capacity);
// Nonblocking: >0 encoded bytes, 0 means try again, <0 error/revocation.
// Descriptor bytes are copied unchanged; their timestamp fields remain unverified.
int ps_capture_read(ps_capture *session, void *destination, size_t capacity, uint8_t descriptor[0x70]);
// Same through sceAvcap2ReadVideo2, the only read that accepts HEVC. Its descriptor is laid out differently.
int ps_capture_read2(ps_capture *session, void *destination, size_t capacity, uint8_t descriptor[0x88]);
// Stop, close, terminate, STOP, unregister. NULL is harmless; consumes the session.
int ps_capture_close(ps_capture *session);

// Ask the encoder for an IDR as soon as possible (sceAvcap2RequestKeyFrame). 0 or error.
int ps_capture_request_keyframe(ps_capture *session);
// Change target and peak bitrate in bit/s at runtime; 0 keeps a value. 0 or error.
int ps_capture_set_bitrate(ps_capture *session, uint32_t average, uint32_t peak);
// Game audio over the same session. encoder: 0 raw PCM, 2 Opus (48 kHz stereo, bitrate in bit/s,
// frame_index 1 = 10 ms, 0 = 20 ms). One audio channel per session; ps_capture_close closes it too.
int ps_capture_audio_open(ps_capture *session, uint32_t encoder, uint32_t bitrate, uint32_t frame_index);
// Research variant: explicit purpose, config word 0x1C and open flag; returns the raw Avcap2 code on failure.
int ps_capture_audio_open_ex(ps_capture *session, uint32_t encoder, uint32_t bitrate, uint32_t frame_index,
                             uint32_t purpose, uint32_t field_1c, uint8_t flag);
// Nonblocking: >0 bytes of one packet, 0 means try again, <0 error. header receives the packet meta.
int ps_capture_audio_read(ps_capture *session, void *destination, size_t capacity, uint8_t header[0x10]);
int ps_capture_audio_close(ps_capture *session);
#ifdef __cplusplus
}
#endif
#endif
