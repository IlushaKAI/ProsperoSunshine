// SPDX-License-Identifier: GPL-3.0-only
// The firmware probe's record builder (platform/ps5/probe_record.h) against configure() itself: with the host's choices it
// must give the same record for every firmware, codec, mode, frame rate, bit depth, bitrate and HEVC variant. Also the
// research hooks in capture.c that the probe uses: the record hook, open only and the number of OpenVideo attempts.
#include "fake_avcap.h"
#include "platform/ps5/probe_record.h"
#include <assert.h>

static uint8_t hook_seen[PS_CAPTURE_RECORD_CAPACITY];
static ptrdiff_t hook_shift = 99;

static void rewrite(uint8_t *record, size_t capacity, ptrdiff_t shift, void *context)
{
    memcpy(hook_seen, record, capacity);
    hook_shift = shift;
    ps_probe_layout *l = context;
    assert(ps_probe_build_record(record, capacity, l) == ps_probe_record_size(l));
}

static int not_active(const char *step, const uint8_t *record)
{
    (void)record;
    return !strcmp(step, "OpenVideo") ? (int)0x81950008 : 0;
}

int main(void)
{
    static const uint32_t firmwares[] = {0x12400000u, 0x13000040u, 0x13400000u, 0x13600000u, 0x10200006u, 0x10400000u, 0x09600004u};
    static const uint32_t rates[] = {20000000u, 150000000u};
    static const int variants[] = {PS_EXP_STANDARD, PS_EXP_MINIMAL, PS_EXP_FULL};
    int compared = 0;
    for (size_t f = 0; f < sizeof firmwares / sizeof firmwares[0]; f++)
        for (uint32_t codec = 0; codec < 2; codec++)
            for (size_t v = 0; v < (codec ? 3u : 1u); v++)
                for (uint32_t resolution = 3; resolution <= 6; resolution++)
                    for (uint32_t timing = 0; timing < 2; timing++)
                        for (uint32_t ten_bit = 0; ten_bit < 2; ten_bit++)
                            for (size_t r = 0; r < 2; r++) {
                                fake_firmware = firmwares[f];
                                ps_capture_set_codec(codec);
                                ps_capture_set_ten_bit(ten_bit);
                                ps_capture_set_video_mode(resolution, timing);
                                ps_capture_set_opening_bitrate(rates[r], rates[r] / 100 * 112);
                                atomic_store(&session_resource[0], atomic_load(&resource_id[0]));
                                uint8_t host[PS_CAPTURE_RECORD_CAPACITY] = {0}, probe[PS_CAPTURE_RECORD_CAPACITY];
                                configure(0, host, 0x13c, variants[v]);
                                const ps_probe_layout l = ps_probe_host_layout(
                                    record_shift(), codec, resolution, timing, ten_bit, (uint32_t)atomic_load(&resource_id[0]),
                                    0x13c, rates[r], rates[r] / 100 * 112, variants[v]);
                                const size_t size = ps_probe_build_record(probe, sizeof probe, &l);
                                // A host that sets 10.x's QP (0xc4/0xc8) picks it from the bitrate; the probe sets it per attempt.
                                if (record_shift() < 0) memcpy(probe + ps_qp_offset(record_shift()), host + ps_qp_offset(record_shift()), 8);
                                if (size != (size_t)(0x2e0 + record_shift()) || memcmp(host, probe, sizeof host)) {
                                    size_t at = 0;
                                    while (at < sizeof host && host[at] == probe[at]) at++;
                                    printf("FAIL: probe record %08x codec=%u variant=%d mode=%u timing=%u ten_bit=%u kbit=%u "
                                           "differs at %#zx\n", firmwares[f], codec, variants[v], resolution, timing, ten_bit,
                                           rates[r] / 1000, at);
                                    return 1;
                                }
                                compared++;
                            }

    // A record larger than the buffer, or too small to hold 13.00's tail, is refused; the largest shift that fits is +0x20.
    ps_probe_layout l = ps_probe_host_layout(0x20, 0, 4, 1, 0, 180, 0x13c, 6000000, 6720000, PS_EXP_STANDARD);
    uint8_t record[PS_CAPTURE_RECORD_CAPACITY];
    assert(ps_probe_build_record(record, sizeof record, &l) == 0x300);
    l.shift = 0x28;
    assert(ps_probe_build_record(record, sizeof record, &l) == 0);
    l.shift = -0x40;
    assert(ps_probe_build_record(record, sizeof record, &l) == 0x2a0);
    l.shift = -0xe8;
    assert(ps_probe_build_record(record, sizeof record, &l) == 0);
    // 10.20's QP fields go only where no rate fields are.
    l = ps_probe_host_layout(-0x18, 0, 4, 1, 0, 180, 0x13c, 6000000, 6720000, PS_EXP_STANDARD);
    l.qp_idr = 24;
    l.qp_other = 28;
    assert(ps_probe_build_record(record, sizeof record, &l) == 0x2c8);
    uint32_t word;
    memcpy(&word, record + 0xc4, 4);
    assert(word == 24);
    memcpy(&word, record + 0xc8, 4);
    assert(word == 28);

    // The hook sees configure()'s record and its shift, and what it writes is what OpenVideo gets.
    fake_firmware = 0x10200006u;
    fake_decide = NULL;
    ps_capture_set_codec(0);
    ps_capture_set_ten_bit(0);
    ps_capture_set_video_mode(4, 1);
    l.shift = -0x10;  // a layout no firmware has
    ps_capture_set_record_hook(rewrite, &l);
    ps_capture *session = NULL;
    fake_opens = 0;
    assert(ps_capture_open(&session) == 0 && session);
    ps_capture_close(session);
    assert(hook_shift == -0x18 && fake_opens == 1);
    uint64_t total;
    memcpy(&total, hook_seen, 8);
    assert(total == 0x2c8);
    memcpy(&total, fake_record, 8);
    assert(total == 0x2d0);
    ps_capture_set_record_hook(NULL, NULL);

    // Open only: the validator's answer without Start.
    ps_capture_set_open_only(1);
    fake_log[0] = 0;
    assert(ps_capture_open(&session) == PS_CAPTURE_ERROR && !session);
    const char *step = NULL;
    assert(ps_capture_last_failure(&step) == 0 && !strcmp(step, "open only"));
    assert(!strstr(fake_log, "Start rc="));
    ps_capture_set_open_only(0);

    // OpenVideo is tried as often as asked while the service answers 0x81950008; 0 restores the host's 25 tries.
    fake_decide = not_active;
    fake_opens = 0;
    ps_capture_set_open_attempts(1);
    assert(ps_capture_open(&session) == PS_CAPTURE_ERROR);
    assert(fake_opens == 1 && ps_capture_last_failure(&step) == (int)0x81950008 && !strcmp(step, "OpenVideo"));
    fake_opens = 0;
    ps_capture_set_open_attempts(0);
    assert(ps_capture_open(&session) == PS_CAPTURE_ERROR && fake_opens == 26);
    fake_decide = NULL;

    printf("PASS: probe record builder equals configure() in %d cases; record hook, open only, open attempts\n", compared);
    return 0;
}
