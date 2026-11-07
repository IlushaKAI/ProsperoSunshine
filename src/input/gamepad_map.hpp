// SPDX-License-Identifier: GPL-3.0-only
// Moonlight gamepad state (Xbox layout, platf::gamepad_state_t) → PS5 virtual DualSense state.
// Portable so it can be unit-tested on the build host; docs/input-mapping.md lists the table.
#pragma once
#include <array>
#include <cstdint>

namespace prospero {

// Moonlight button bits (Sunshine's platform/common.h).
namespace moonlight {
inline constexpr uint32_t dpad_up = 0x0001, dpad_down = 0x0002, dpad_left = 0x0004, dpad_right = 0x0008;
inline constexpr uint32_t start = 0x0010, back = 0x0020, left_stick = 0x0040, right_stick = 0x0080;
inline constexpr uint32_t left_button = 0x0100, right_button = 0x0200, home = 0x0400;
inline constexpr uint32_t a = 0x1000, b = 0x2000, x = 0x4000, y = 0x8000;
inline constexpr uint32_t touchpad = 0x100000;
}

// ScePadButton bits (src/platform/ps5/pad.h).
namespace dualsense {
inline constexpr uint32_t create = 0x1, l3 = 0x2, r3 = 0x4, options = 0x8;
inline constexpr uint32_t up = 0x10, right = 0x20, down = 0x40, left = 0x80;
inline constexpr uint32_t l2 = 0x100, r2 = 0x200, l1 = 0x400, r1 = 0x800;
inline constexpr uint32_t triangle = 0x1000, circle = 0x2000, cross = 0x4000, square = 0x8000;
inline constexpr uint32_t ps = 0x10000, touchpad = 0x100000;
}

struct PadState {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2;
};

// The host's pad plays as the console's user; every other pad is a guest the console asked about (pad_roles.hpp). Unless
// the owner allows it for all (ps_button_all_players), a guest's PS and Create don't go through, so a guest can't leave
// the game, open the control center, reach the settings or open the Create menu, which captures and broadcasts what the
// console shows. Sunshine has already turned a held Select into home here, so that way is closed too.
enum class PadRole { host, guest };
constexpr uint32_t allowed_buttons(uint32_t flags, PadRole role, bool all_players) {
    return role == PadRole::host || all_players ? flags : flags & ~(moonlight::home | moonlight::back);
}

// A trigger counts as pressed (the digital L2/R2 bit) above this analog value.
inline constexpr uint8_t trigger_threshold = 8;

// A guest's pad opens on the console only once its controller is used (GitHub issue 26): some clients announce a
// controller that never sends anything (Steam Input's virtual one next to the real one, on-screen controls), and the
// console's "Who's using this controller?" for it can only be answered by that controller, so it blocked the screen.
// A button, a trigger past the threshold or a stick pushed a quarter of the way counts; stick drift doesn't.
inline constexpr int stick_wake = 8192;
constexpr bool wakes_pad(uint32_t flags, uint8_t lt, uint8_t rt, int16_t ls_x, int16_t ls_y, int16_t rs_x, int16_t rs_y) {
    const auto far = [](int16_t v) { return v >= stick_wake || v <= -stick_wake; };
    return flags || lt >= trigger_threshold || rt >= trigger_threshold || far(ls_x) || far(ls_y) || far(rs_x) || far(rs_y);
}

// Moonlight sticks are signed 16-bit with +Y up; DualSense sticks are 0..255 with 0 at the top.
constexpr uint8_t stick_axis(int16_t value, bool invert) {
    int v = (static_cast<int>(value) + 32768) >> 8;  // 0..255
    if (invert) v = 255 - v;
    return static_cast<uint8_t>(v);
}

constexpr PadState map_gamepad(uint32_t flags, uint8_t lt, uint8_t rt, int16_t ls_x, int16_t ls_y, int16_t rs_x,
                               int16_t rs_y) {
    struct Pair { uint32_t from, to; };
    constexpr Pair table[] = {
        {moonlight::a, dualsense::cross},         {moonlight::b, dualsense::circle},
        {moonlight::x, dualsense::square},        {moonlight::y, dualsense::triangle},
        {moonlight::dpad_up, dualsense::up},      {moonlight::dpad_down, dualsense::down},
        {moonlight::dpad_left, dualsense::left},  {moonlight::dpad_right, dualsense::right},
        {moonlight::left_button, dualsense::l1},  {moonlight::right_button, dualsense::r1},
        {moonlight::left_stick, dualsense::l3},   {moonlight::right_stick, dualsense::r3},
        {moonlight::start, dualsense::options},   {moonlight::back, dualsense::create},
        {moonlight::home, dualsense::ps},         {moonlight::touchpad, dualsense::touchpad},
    };
    uint32_t buttons = 0;
    for (const auto &p : table)
        if (flags & p.from) buttons |= p.to;
    if (lt >= trigger_threshold) buttons |= dualsense::l2;
    if (rt >= trigger_threshold) buttons |= dualsense::r2;
    return {buttons, stick_axis(ls_x, false), stick_axis(ls_y, true), stick_axis(rs_x, false), stick_axis(rs_y, true),
            lt, rt};
}

// Moonlight's event flags in its trigger effect message.
inline constexpr uint8_t trigger_flag_right = 0x04, trigger_flag_left = 0x08;

// The block the console keeps for a pad: a mask, then mode and ten parameters for L2 and for R2, one spare byte
// (docs/re/adaptive-triggers.md).
using TriggerBlock = std::array<uint8_t, 24>;
inline constexpr TriggerBlock triggers_off = {0x03, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x05};

struct TriggerEffects {
    uint8_t flags;  // the triggers that differ from the last block
    uint8_t type_left, type_right;
    std::array<uint8_t, 10> left, right;
};

// Effect 0x26 knocks against the finger, with a 3-bit strength for each of ten zones in bytes 2 to 5. The console turns effects down
// by its own setting; a client writes them to the controller as they are, and a game's full strength is a hard knock. percent is
// the app's trigger strength.
inline constexpr uint8_t trigger_effect_knock = 0x26;
constexpr void soften_knock(uint8_t type, std::array<uint8_t, 10> &data, int percent) {
    if (type != trigger_effect_knock || percent >= 100) return;
    uint32_t packed = data[2] | uint32_t(data[3]) << 8 | uint32_t(data[4]) << 16 | uint32_t(data[5]) << 24, softer = packed & 0xc0000000u;
    for (int zone = 0; zone < 10; ++zone) softer |= (((packed >> (3 * zone) & 7u) * uint32_t(percent) + 50) / 100) << (3 * zone);
    for (int i = 0; i < 4; ++i) data[2 + i] = uint8_t(softer >> (8 * i));
}

// The app's trigger force in percent as a DualSense's power reduction: steps of an eighth, and 7 is as far as it goes.
constexpr int trigger_power_steps(int percent) {
    const int steps = ((100 - percent) * 8 + 50) / 100;
    return steps < 0 ? 0 : steps > 7 ? 7 : steps;
}

constexpr TriggerEffects map_trigger_effects(const TriggerBlock &block, const TriggerBlock &last, int knock_percent = 100) {
    TriggerEffects effects{};
    effects.type_left = block[1];
    effects.type_right = block[12];
    for (int i = 0; i < 10; ++i) {
        effects.left[i] = block[2 + i];
        effects.right[i] = block[13 + i];
    }
    soften_knock(effects.type_left, effects.left, knock_percent);
    soften_knock(effects.type_right, effects.right, knock_percent);
    for (int i = 1; i <= 11; ++i) {
        if (block[i] != last[i]) effects.flags |= trigger_flag_left;
        if (block[11 + i] != last[11 + i]) effects.flags |= trigger_flag_right;
    }
    return effects;
}

}
