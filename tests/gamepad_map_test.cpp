// SPDX-License-Identifier: GPL-3.0-only
#include "input/gamepad_map.hpp"
#include <cassert>
#include <iostream>
using namespace prospero;

int main() {
    {
        // The knock of a shot, as Uncharted sends it: strength 7 in every zone comes out as 5, the rest of the block as it was.
        TriggerBlock knock{0x03, 0x26, 0xff, 0x03, 0xff, 0xff, 0xff, 0x3f, 0, 0, 1, 0, 0x26, 0xff, 0x03, 0xff, 0xff, 0xff, 0x3f, 0, 0, 1, 0, 0};
        assert(map_trigger_effects(knock, triggers_off).left[2] == 0xff);  // 100%: as the game sent it
        const auto soft = map_trigger_effects(knock, triggers_off, 70);
        assert(soft.type_left == 0x26 && soft.left[0] == 0xff && soft.left[1] == 0x03 && soft.left[8] == 1);
        const uint32_t packed = soft.left[2] | uint32_t(soft.left[3]) << 8 | uint32_t(soft.left[4]) << 16 | uint32_t(soft.left[5]) << 24;
        for (int zone = 0; zone < 10; ++zone) assert((packed >> (3 * zone) & 7u) == 5);
        assert(soft.right == soft.left);
        // Other effects stay as they are.
        TriggerBlock resistance{0x03, 0x21, 0xe0, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0x25, 0x14, 0, 1};
        const auto same = map_trigger_effects(resistance, triggers_off, 70);
        assert(same.left[0] == 0xe0 && same.left[1] == 0x03 && same.right[0] == 0x14 && same.right[2] == 1);
    }
    const auto idle = map_gamepad(0, 0, 0, 0, 0, 0, 0);
    assert(idle.buttons == 0);
    assert(idle.lx == 128 && idle.ly == 127 && idle.rx == 128 && idle.ry == 127);  // centre within one step
    const auto face = map_gamepad(moonlight::a | moonlight::b | moonlight::x | moonlight::y, 0, 0, 0, 0, 0, 0);
    assert(face.buttons == (dualsense::cross | dualsense::circle | dualsense::square | dualsense::triangle));
    const auto system = map_gamepad(moonlight::home | moonlight::start | moonlight::back | moonlight::touchpad, 0, 0, 0,
                                    0, 0, 0);
    assert(system.buttons == (dualsense::ps | dualsense::options | dualsense::create | dualsense::touchpad));
    const auto corners = map_gamepad(0, 0, 0, -32768, 32767, 32767, -32768);
    assert(corners.lx == 0 && corners.ly == 0);      // full left, full up
    assert(corners.rx == 255 && corners.ry == 255);  // full right, full down
    const auto triggers = map_gamepad(0, trigger_threshold - 1, 255, 0, 0, 0, 0);
    assert(!(triggers.buttons & dualsense::l2) && (triggers.buttons & dualsense::r2));
    assert(triggers.l2 == trigger_threshold - 1 && triggers.r2 == 255);
    // As Teardown sends it: resistance on the second block only.
    const TriggerBlock feedback = {0x03, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x21, 0xfe, 0x03, 0x48, 0x92, 0x24, 0x09};
    const auto on = map_trigger_effects(feedback, triggers_off);
    assert(on.flags == trigger_flag_right);
    assert(on.type_left == 0x05 && on.type_right == 0x21);
    assert(on.right[0] == 0xfe && on.right[5] == 0x09 && on.right[9] == 0 && on.left[0] == 0);
    assert(map_trigger_effects(triggers_off, feedback).flags == trigger_flag_right);
    assert(map_trigger_effects(triggers_off, TriggerBlock{}).flags == (trigger_flag_left | trigger_flag_right));
    TriggerBlock left_only = triggers_off;
    left_only[11] = 1;  // last parameter of the first block
    assert(map_trigger_effects(left_only, triggers_off).flags == trigger_flag_left);
    TriggerBlock mask_only = triggers_off;
    mask_only[0] = 0x01;
    mask_only[23] = 0xff;
    assert(map_trigger_effects(mask_only, triggers_off).flags == 0);
    // Only the host has PS and Create unless every player may.
    constexpr uint32_t with_home = moonlight::home | moonlight::a | moonlight::back | moonlight::start;
    static_assert(allowed_buttons(with_home, PadRole::host, false) == with_home);
    static_assert(allowed_buttons(with_home, PadRole::guest, false) == (moonlight::a | moonlight::start));
    static_assert(allowed_buttons(with_home, PadRole::guest, true) == with_home);
    static_assert(allowed_buttons(0, PadRole::guest, false) == 0);
    // A guest's pad opens on its first use: a button, a trigger or a stick pushed far enough, not drift (issue 26).
    static_assert(!wakes_pad(0, 0, 0, 0, 0, 0, 0));
    static_assert(!wakes_pad(0, trigger_threshold - 1, 0, 3000, -3000, stick_wake - 1, -(stick_wake - 1)));
    static_assert(wakes_pad(moonlight::a, 0, 0, 0, 0, 0, 0));
    static_assert(wakes_pad(0, 0, trigger_threshold, 0, 0, 0, 0));
    static_assert(wakes_pad(0, 0, 0, 0, -stick_wake, 0, 0) && wakes_pad(0, 0, 0, 0, 0, 0, -32768));
    std::cout << "PASS: gamepad mapping (face, system, sticks, triggers, trigger effects, PS and Create for the host only, "
                 "a guest's pad opens on its first use)\n";
}
