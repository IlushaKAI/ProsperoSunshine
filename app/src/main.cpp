// SPDX-License-Identifier: GPL-3.0-only
// ProsperoSunshine's PS5 app: sets the host's sunshine.conf, shows the host's state and its pairing
// requests, takes Moonlight's PIN, and turns the Payload Manager's autoload of the host on and off.
// The frame loop is ps5-homebrew-ui's (its src/main.cpp, cut down): read the pad, update the screen,
// play the cues it asked for, draw, present. All waiting on the host happens on psapp::Host's worker.
//
// For unattended tests the app ends itself when /data/prosperosunshine/app.quit appears (the file
// is removed first), as PS5SD does with its quit file.

#include "app/psapp/host.hpp"
#include "app/psapp/pad_log.hpp"
#include "app/psapp/screen.hpp"
#include "audio/cues.hpp"
#include "audio/mixer.hpp"
#include "core/input.hpp"
#include "core/save_file.hpp"
#include "core/version.hpp"
#include "gfx/renderer.hpp"
#include "platform/ps5/audio_out.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include "ui/fonts.hpp"

#include <GL/glcorearb.h>

#include <cstdio>
#include <span>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace
{

constexpr const char *kAssets = "/app0/assets";
constexpr const char *kQuitFile = "/data/prosperosunshine/app.quit";
// Where a title has no /data (9.60); the host removes it.
constexpr const char *kInstallQuitFile = "/app0/app.quit";

bool load_font(hui::gfx::Renderer &renderer, const char *name, hui::gfx::Font *font, hui::ui::FontRef *ref)
{
    std::string data;
    const std::string path = std::string(kAssets) + "/fonts/" + name;
    if (!hui::save::read_file(path, &data) || !font->load(data))
    {
        hui::sys::log("[PSAPP] font %s failed: %s", name, font->error().c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

} // namespace

int main()
{
    using namespace hui;
    sys::log("[PSAPP] entry");

    ps5::Display display;
    if (!display.open(1920, 1080))
    {
        sys::log("[PSAPP] fatal: display open failed");
        sys::park();
    }

    gfx::Renderer renderer;
    gfx::Font regular, semibold, display_font, mono, pixel, hand;
    ui::Fonts fonts;
    if (!renderer.init() || !load_font(renderer, "inter-regular.huifont", &regular, &fonts.regular) ||
        !load_font(renderer, "inter-semibold.huifont", &semibold, &fonts.semibold) ||
        !load_font(renderer, "montserrat-medium.huifont", &display_font, &fonts.display) ||
        !load_font(renderer, "dejavu-sans-mono.huifont", &mono, &fonts.mono) ||
        !load_font(renderer, "press-start-2p.huifont", &pixel, &fonts.pixel) ||
        !load_font(renderer, "patrick-hand.huifont", &hand, &fonts.hand))
    {
        sys::log("[PSAPP] fatal: renderer init failed");
        sys::park();
    }

    ps5::Pad pad;
    pad.open();
    psapp::PadLog pad_log;
    InputTracker tracker;
    audio::Mixer mixer;
    ps5::AudioOut audio_out;
    audio_out.start(mixer);
    audio::SoundBank sounds;
    const auto bank = sounds.load(std::string(kAssets) + "/audio/sfx");
    sys::log("[PSAPP] sounds files=%d rejected=%d", bank.files, bank.rejected);

    psapp::Host host;
    host.start();
    psapp::Screen screen(host, fonts);
    const std::string version = read_content_version("/app0/sce_sys/param.json");
    screen.set_version(version);
    host.set_app_version(version);
    sys::log("[PSAPP] version %s", version.empty() ? "unknown" : version.c_str());

    gfx::BackdropSpec sky;
    sky.mode = gfx::BackdropMode::aurora;
    sky.colors[0] = gfx::Color::rgb(0x070b18);
    sky.colors[1] = gfx::Color::rgb(0x111833);
    sky.colors[2] = gfx::Color::rgb(0x3b2a5a);
    sky.colors[3] = gfx::Color::rgb(0x7a4a1c);

    gfx::DrawList background, page, sheet;
    ui::Feedback feedback;
    PadSample samples[64];
    std::int64_t last = sys::monotonic_us();
    const std::int64_t start = last;
    std::uint64_t frames = 0;
    for (;;)
    {
        const std::int64_t now = sys::monotonic_us();
        float dt = frames == 0 ? 1.0f / 60.0f : static_cast<float>(now - last) / 1e6f;
        last = now;
        if (dt > 0.05f)
            dt = 0.05f; // a hitch must not teleport the animations
        const std::size_t count = pad.read(samples);
        pad_log.tick(now);
        const InputFrame input =
            tracker.update(std::span<const PadSample>(samples, count), static_cast<std::uint64_t>(now));

        feedback.clear();
        screen.update(input, dt, feedback);
        for (const audio::CueEvent &event : feedback.cues)
            sounds.play(mixer, event.set == audio::SoundSet::count ? audio::SoundSet::glass : event.set, event);
        if (feedback.rumble_strength > 0.0f)
            pad.rumble(feedback.rumble_strength, feedback.rumble_seconds);
        pad.tick(dt);

        // The sky and its light, frosted for the cards; the page; the sheet over the frosted page.
        background.clear();
        page.clear();
        sheet.clear();
        screen.draw_background(background);
        screen.draw_page(page, renderer.glass_texture());
        if (screen.has_sheet())
            screen.draw_sheet(sheet, renderer.glass_texture());
        sky.time = static_cast<float>(now - start) / 1e6f;
        renderer.begin();
        renderer.backdrop(sky);
        renderer.draw(background);
        renderer.glass();
        renderer.draw(page);
        if (screen.has_sheet())
        {
            renderer.glass();
            renderer.draw(sheet);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        renderer.present(0, display.width(), display.height());
        if (!display.swap())
        {
            sys::log("[PSAPP] fatal: swap failed error=%s", ps5::egl_error_name(display.last_error()));
            sys::park();
        }
        if (++frames == 1)
            sys::log("[PSAPP] ready splash_hidden=%d", sys::hide_splash_screen() ? 1 : 0);
        if (frames % 60 == 0)
        {
            struct stat info{};
            if (::stat(kQuitFile, &info) == 0 || ::stat(kInstallQuitFile, &info) == 0)
            {
                ::unlink(kQuitFile);
                sys::log("[PSAPP] closing: quit file");
                pad.close();
                sys::quit();
            }
        }
    }
}
