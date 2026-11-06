// SPDX-License-Identifier: GPL-3.0-only
// Turns packets of the haptic channel into levels for the two classic motors (docs/re/haptics.md). Packets are float stereo. Most of
// what a game sends is a click of 20 to 40 ms, so peaks count, not the mean, and a level falls fast. The low part goes to the
// large motor and the rest to the small one, which keeps a click from turning into a soft rumble.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace prospero {

struct HapticLevels {
    float low = 0, high = 0;  // peak of each part, 0 to 1
};

class HapticSplitter {
public:
    HapticLevels process(const float *samples, std::size_t frames) {
        HapticLevels levels;
        for (std::size_t i = 0; i < frames; ++i) {
            const float left = samples[2 * i], right = samples[2 * i + 1];
            float sample = std::fabs(left) >= std::fabs(right) ? left : right;
            if (!std::isfinite(sample)) sample = 0;
            low_ += kLowPass * (sample - low_);
            levels.low = std::max(levels.low, std::fabs(low_));
            levels.high = std::max(levels.high, std::fabs(sample - low_));
        }
        levels.low = std::min(levels.low, 1.0f);
        levels.high = std::min(levels.high, 1.0f);
        return levels;
    }

private:
    static constexpr float kLowPass = 0.061f;  // about 120 Hz at the channel's 12 kHz
    float low_ = 0;
};

// feed() from the reader for every packet, tick() every 10 ms from the pad thread: rises at once, gone after about 60 ms. Only what
// stands out of the last quarter second counts, so a lasting sound doesn't become a hum. Levels are linear and capped at half of
// a motor's range: at full range a DualSense over Bluetooth drowns everything else. Steps of 16 keep small changes off the network.
class HapticEnvelope {
public:
    // The app's vibration strength in percent, at any time.
    void set_strength(int percent) { strength_ = std::clamp(percent, 0, 200); }

    void feed(HapticLevels levels) {
        raise(low_in_, stands_out(low_base_, levels.low));
        raise(high_in_, stands_out(high_base_, levels.high));
    }

    // Large motor, small motor.
    std::array<std::uint8_t, 2> tick() {
        low_ = std::max(low_ * kFall, take(low_in_));
        high_ = std::max(high_ * kFall, take(high_in_));
        return {motor(low_, kLowGain), motor(high_, kHighGain)};
    }

private:
    static constexpr float kFall = 0.6f, kFloor = 0.03f, kLowGain = 0.8f, kHighGain = 0.6f;
    static constexpr float kBaseRate = 0.08f, kBaseShare = 0.85f;
    static constexpr int kStep = 16, kCap = 128;

    static float stands_out(float &base, float level) {
        base += kBaseRate * (level - base);
        return std::max(0.0f, level - kBaseShare * base);
    }
    static void raise(std::atomic<std::uint32_t> &slot, float level) {
        const auto scaled = static_cast<std::uint32_t>(std::clamp(level, 0.0f, 1.0f) * 65535.0f);
        auto seen = slot.load(std::memory_order_relaxed);
        while (seen < scaled && !slot.compare_exchange_weak(seen, scaled, std::memory_order_relaxed)) {}
    }
    static float take(std::atomic<std::uint32_t> &slot) { return static_cast<float>(slot.exchange(0, std::memory_order_relaxed)) / 65535.0f; }
    std::uint8_t motor(float level, float gain) const {
        if (level < kFloor) return 0;
        const int strength = strength_.load(std::memory_order_relaxed);
        const int value = static_cast<int>(level * gain * 255.0f) * strength / 100;
        return static_cast<std::uint8_t>(std::min(std::min(255, kCap * strength / 100), (value + kStep - 1) / kStep * kStep));
    }

    std::atomic<std::uint32_t> low_in_{0}, high_in_{0};
    float low_ = 0, high_ = 0, low_base_ = 0, high_base_ = 0;
    std::atomic<int> strength_{100};
};

}
