// SPDX-License-Identifier: GPL-3.0-only
// The capture records must not change while the Experimental switches are off: every firmware, codec, mode, frame rate,
// bit depth and bitrate below, from configure() and as OpenVideo receives them, hashed and compared with
// tests/capture_records.golden, taken from the code before the switches existed. Only 10.x's lines were taken again
// when its QP came to follow the bitrate (cfg+0xc4 and +0xc8, platform/ps5/encoder_qp.h), and 10.x's H.264 lines when
// the slice fields came (cfg+0x40, +0x44, +0x48). 13.40's lines are 13.60's, whose record it takes since GitHub issue 14.
// 9.60's lines came with its own record and QP fields, from the firmware probe.
// `--hashes` prints those lines, `--full` the records themselves, for a byte-wise comparison with another build.
#include "fake_avcap.h"

// The code before the switches passes no variant.
#ifndef CONFIGURE
#define CONFIGURE(record, output) configure(0, record, output, PS_EXP_STANDARD)
#endif

static uint64_t fnv1a(const uint8_t *bytes, size_t size)
{
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < size; i++) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

static void hex(const uint8_t *bytes, size_t size)
{
    for (size_t i = 0; i < size; i++) printf("%02x", bytes[i]);
    putchar('\n');
}

int main(int argc, char **argv)
{
    static const uint32_t firmwares[] = {0x12400000u, 0x13000040u, 0x13400002u, 0x13600000u, 0x10200006u, 0x10400000u, 0x09600000u};
    static const uint32_t rates[] = {20000000u, 150000000u};
    const int hashes = argc > 1 && !strcmp(argv[1], "--hashes"), full = argc > 1 && !strcmp(argv[1], "--full");
    FILE *golden = hashes || full ? NULL : fopen(GOLDEN, "r");
    if (!hashes && !full && !golden) { puts("FAIL: no " GOLDEN); return 1; }
    int count = 0, failures = 0;
    for (size_t f = 0; f < sizeof firmwares / sizeof firmwares[0]; f++)
        for (uint32_t codec = 0; codec < 2; codec++)
            for (uint32_t resolution = 4; resolution <= 6; resolution++)
                for (uint32_t timing = 0; timing < 2; timing++)
                    for (uint32_t ten_bit = 0; ten_bit < 2; ten_bit++)
                        for (size_t r = 0; r < 2; r++) {
                        fake_firmware = firmwares[f];
                        ps_capture_set_codec(codec);
                        ps_capture_set_ten_bit(ten_bit);
                        ps_capture_set_video_mode(resolution, timing);
                        const uint32_t average = rates[r];
                        ps_capture_set_opening_bitrate(average, average / 100 * 112);
                        atomic_store(&session_resource[0], atomic_load(&resource_id[0]));
                        uint8_t configured[0x2e8] = {0};
                        CONFIGURE(configured, 0x13c);
                        // The probes' builder with the same values must write the same record.
                        const ps_capture_video_config explicit_values = {resolution, timing, codec, ten_bit, average, average / 100 * 112,
                                                                         atomic_load(&resource_id[0]), 0};
                        uint8_t built[0x2e8] = {0};
                        ps_capture_build_config(built, &explicit_values, 0x13c, PS_EXP_STANDARD);
                        if (memcmp(built, configured, sizeof built)) {
                            printf("FAIL: ps_capture_build_config differs from configure() for %08x codec=%u mode=%u\n", firmwares[f],
                                   codec, resolution);
                            failures++;
                        }
                        ps_capture *session = NULL;
                        memset(fake_record, 0, sizeof fake_record);
                        const int rc = ps_capture_open(&session);
                        ps_capture_close(session);
                        fake_log[0] = 0;
                        char label[96];
                        snprintf(label, sizeof label, "%08x codec=%u mode=%u timing=%u ten_bit=%u kbit=%u rc=%d", firmwares[f], codec,
                                 resolution, timing, ten_bit, average / 1000, rc);
                        if (full) {
                            printf("%s configure ", label);
                            hex(configured, sizeof configured);
                            printf("%s open ", label);
                            hex(fake_record, sizeof fake_record);
                            continue;
                        }
                        char want[160], have[160];
                        snprintf(have, sizeof have, "%s %016llx %016llx\n", label,
                                 (unsigned long long)fnv1a(configured, sizeof configured),
                                 (unsigned long long)fnv1a(fake_record, sizeof fake_record));
                        if (hashes) {
                            fputs(have, stdout);
                            continue;
                        }
                        if (!fgets(want, sizeof want, golden) || strcmp(want, have)) {
                            printf("FAIL: record changed: %s", have);
                            failures++;
                        }
                        count++;
                    }
    if (golden) {
        fclose(golden);
        if (failures) return 1;
        printf("PASS: %d capture records as before the Experimental switches\n", count);
    }
    return 0;
}
