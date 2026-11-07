// SPDX-License-Identifier: GPL-3.0-only
// From haptic packets to motor levels: a click stays short, silence and the near-zero tail stay silent, low and high go apart.
#include "platform/ps5/haptic_envelope.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>

using namespace prospero;

// Ten packets of samples at 3 kHz: long enough for the filter to settle.
static constexpr std::size_t kToneFrames = 10 * kHapticFrames;
static std::vector<float> tone(float frequency, float level) {
    std::vector<float> samples(2 * kToneFrames);
    for (std::size_t i = 0; i < kToneFrames; ++i) samples[2 * i] = samples[2 * i + 1] = level * std::sin(2 * 3.14159265f * frequency * static_cast<float>(i) / 3000.0f);
    return samples;
}

int main() {
    // Silence, and the tiny values a game's sound leaves behind: these once read as a constant rumble.
    HapticSplitter splitter;
    HapticEnvelope envelope;
    std::vector<float> packet(512, 0.0f);
    envelope.feed(splitter.process(packet.data(), 256));
    assert((envelope.tick() == std::array<std::uint8_t, 2>{0, 0}));
    packet.assign(512, 3.6e-29f);
    envelope.feed(splitter.process(packet.data(), 256));
    assert((envelope.tick() == std::array<std::uint8_t, 2>{0, 0}));
    packet.assign(512, 1e-4f);
    envelope.feed(splitter.process(packet.data(), 256));
    assert((envelope.tick() == std::array<std::uint8_t, 2>{0, 0}));
    packet.assign(512, std::numeric_limits<float>::quiet_NaN());
    envelope.feed(splitter.process(packet.data(), 256));
    assert((envelope.tick() == std::array<std::uint8_t, 2>{0, 0}));

    // Low and high go to different motors.
    HapticSplitter bass_splitter, click_splitter;
    auto bass = tone(50, 0.2f), click = tone(1000, 0.2f);
    const auto low = bass_splitter.process(bass.data(), kToneFrames), high = click_splitter.process(click.data(), kToneFrames);
    assert(low.low > low.high && high.high > 3 * high.low);

    // A click: felt at once, the next tick not higher, nothing left after 100 ms.
    HapticEnvelope pulse;
    pulse.feed({0, 0.2f});
    const auto first = pulse.tick();
    assert(first[1] >= 16 && first[0] == 0 && first[1] % 16 == 0);
    auto last = first;
    int ticks = 0;
    while (last != std::array<std::uint8_t, 2>{0, 0}) {
        const auto next = pulse.tick();
        assert(next[0] <= last[0] && next[1] <= last[1]);
        last = next;
        assert(++ticks <= 10);
    }

    // The strongest shot stays under the cap.
    HapticEnvelope shot;
    shot.feed({1, 1});
    const auto strongest = shot.tick();
    assert(strongest[0] <= 128 && strongest[1] <= 128 && strongest[0] > 0);

    // A packet in the controller's format: 8 bit, left and right in turn, clipped at the top, nothing from a bad sample.
    std::vector<float> full(2 * kHapticFrames);
    for (std::size_t i = 0; i < kHapticFrames; ++i) {
        full[2 * i] = 0.5f;
        full[2 * i + 1] = -0.25f;
    }
    std::array<std::uint8_t, kHapticBlockBytes> block{};
    haptic_block(full.data(), 1.0f, block.data());
    assert(static_cast<std::int8_t>(block[0]) == 63 && static_cast<std::int8_t>(block[1]) == -31);
    haptic_block(full.data(), 4.0f, block.data());
    assert(static_cast<std::int8_t>(block[126]) == 127 && static_cast<std::int8_t>(block[127]) == -127);
    full[0] = std::numeric_limits<float>::infinity();
    haptic_block(full.data(), 1.0f, block.data());
    assert(block[0] == 0);

    // The app's strength: half, none, and double up to a motor's range.
    HapticEnvelope half, none, twice;
    half.set_strength(50);
    none.set_strength(0);
    twice.set_strength(200);
    half.feed({1, 1});
    none.feed({1, 1});
    twice.feed({1, 1});
    assert(half.tick()[0] <= 64);
    assert((none.tick() == std::array<std::uint8_t, 2>{0, 0}));
    assert(twice.tick()[0] > strongest[0]);

    // A sound that lasts fades out of the motors: no hum.
    HapticEnvelope lasting;
    std::array<std::uint8_t, 2> during{};
    for (int i = 0; i < 100; ++i) {  // about 2 s of packets
        lasting.feed({0.2f, 0.3f});
        during = lasting.tick();
        lasting.tick();
    }
    assert((during == std::array<std::uint8_t, 2>{16, 16} || during == std::array<std::uint8_t, 2>{0, 16} || during == std::array<std::uint8_t, 2>{0, 0}));

    puts("PASS: haptic packets to motor levels (click, silence, near-zero tail, cap, no hum)");
    return 0;
}
