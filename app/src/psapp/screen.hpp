// SPDX-License-Identifier: GPL-3.0-only
// The app's screen, drawn with ps5-homebrew-ui in its 1920x1080 virtual canvas. Three pages under a
// tab bar (L1/R1):
//   Home      a hero with the host's state and the sun emblem, the host's actions (start or stop,
//             restart, start at boot), pairing requests as cards (Cross: the PIN, Square: deny it), the running streams
//             while someone streams (Cross: a list with what each gets, to end one) and the paired
//             devices (Cross: a list to rename or remove them);
//   Settings  one tile per sunshine.conf option (Cross opens its editor, Square puts it back to the
//             host's default), the Experimental section's switches among them (Cross flips one), then
//             Save, Save and restart, Revert, and the host's web page (a sheet with its address, to turn
//             it on or off, set or change its login, or forget it);
//   Diagnostics the report for a bug report, to read here, as a file and over the network.
// Editors and the PIN come up as a sheet over the page: a list for choices, a stepper for numbers,
// the on-screen keyboard for text and the PIN (R2 or Options: Done; R1 the symbols, L2 shift; R3 shows
// the web page's password while it is typed). The page is frosted behind cards and sheets (the
// kit's glass), and focus, pages, sheets and messages move with springs and fades.
#pragma once

#include "app/psapp/experimental_list.h"
#include "app/psapp/host.hpp"
#include "core/input.hpp"
#include "gfx/draw_list.hpp"
#include "ui/components/component.hpp"
#include "ui/components/keyboard.hpp"
#include "ui/feedback.hpp"
#include "ui/fonts.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace psapp
{

class Screen
{
  public:
    Screen(Host &host, const hui::ui::Fonts &fonts);

    void update(const hui::InputFrame &input, float dt, hui::ui::Feedback &feedback);
    // Three layers, drawn in this order with a glass capture after the first and (when has_sheet())
    // after the second: the light behind the page, the page, the sheet over it.
    void draw_background(hui::gfx::DrawList &list) const;
    void draw_page(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void draw_sheet(hui::gfx::DrawList &list, std::uint32_t glass) const;
    bool has_sheet() const
    {
        return sheet_amount_ > 0.01f;
    }
    void set_version(std::string version)
    {
        version_ = std::move(version);
    }

  private:
    enum class Page
    {
        home,
        settings,
        diagnostics,
    };
    enum class Sheet
    {
        none,
        pin,
        text,
        choice,
        number,
        devices,
        remove_device,
        streams,
        end_stream,
        deny_pairing, // asks before a pending request is denied
        web,          // the web page: its state and address, Turn on or off, Set login, Forget login
        forget_web,   // asks before the web page's login is removed
    };
    enum class Action
    {
        start_stop,
        restart,
        autoload,
        save,
        save_restart,
        revert,
        web, // opens the web page's sheet
    };
    // The web page's sheet: its rows, and which step of setting a login the text sheet is at.
    enum class WebRow
    {
        toggle,    // Turn on (a login is stored, the page is off) or Turn off
        set_login, // Set login, or Change login with one stored
        forget,    // Forget login
        close,
    };
    enum class WebStep
    {
        none,
        user,
        password,
    };

    // Input.
    void switch_page(Page page, hui::ui::Feedback &feedback);
    void handle_home(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void handle_settings(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void handle_diagnostics(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void handle_sheet(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void run(Action action, hui::ui::Feedback &feedback);
    void open_tile(int index);
    void open_sheet(Sheet kind, const std::string &title, const std::string &help, const std::string &initial);
    void close_sheet();
    void open_devices();
    void open_streams();
    void open_web();
    void handle_web(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    bool web_row_enabled(WebRow row) const;
    std::string web_address() const; // https://<console>:<port>, or a line saying where when the address is unknown

    // State helpers.
    bool busy() const
    {
        return !snap_.busy.empty();
    }
    bool dirty() const;
    // A client is connected: the host streams. A host that reports its client (client=) and names
    // none keeps the app open for a client that left: a paused session, not a stream.
    bool live() const
    {
        return snap_.streaming && !(snap_.client_known && snap_.client.empty());
    }
    std::string value_text(int index) const;
    // An Experimental switch on firmware where its function is verified: shown as such, and inert.
    bool switch_not_needed(int index) const;
    hui::gfx::Rect focus_rect() const;
    hui::gfx::Rect action_rect(int index) const;
    hui::gfx::Rect pairing_rect(int index) const;
    hui::gfx::Rect streams_rect() const; // the home page's card of the running streams
    hui::gfx::Rect devices_card() const; // the list sheet: paired devices or running streams
    hui::gfx::Rect device_row_rect(int visible) const; // visible: counted from the first row in view
    hui::gfx::Rect tile_rect(int index) const;
    hui::gfx::Rect settings_action_rect(int index) const;
    hui::gfx::Rect web_row_rect(int index) const;
    int visible_pairings() const;
    // The home page's lower row: the pairing cards, the streams card while someone streams, the paired devices.
    bool streams_shown() const
    {
        return !snap_.streams.empty();
    }
    int devices_index() const
    {
        return visible_pairings() + (streams_shown() ? 1 : 0);
    }
    int list_rows() const; // the rows of the list sheet shown
    float appear(int order) const; // 0..1, the staggered entrance of the page's parts

    // Drawing.
    void draw_top_bar(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void draw_home(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void draw_settings(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void draw_diagnostics(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void draw_web(hui::gfx::DrawList &list, const hui::gfx::Rect &card) const;
    void draw_button(hui::gfx::DrawList &list, std::uint32_t glass, const hui::gfx::Rect &r, int icon,
                     const std::string &label, bool primary, float focus, bool enabled) const;
    void draw_footer(hui::gfx::DrawList &list, std::uint32_t glass) const;
    void glass_card(hui::gfx::DrawList &list, std::uint32_t glass, const hui::gfx::Rect &r, float radius,
                    float focus) const;

    Host &host_;
    const hui::ui::Fonts &fonts_;
    Snapshot snap_;
    Values edits_;
    bool edits_loaded_ = false;
    bool reload_edits_ = false; // the next outcome replaces the edits with the files
    std::uint64_t seen_message_ = 0;
    std::size_t seen_pairings_ = 0;
    float message_age_ = 100.0f;
    int armed_ = -1; // the Action whose first press waits for a second one (it ends a stream)
    float armed_age_ = 100.0f;
    std::string version_;
    float time_ = 0.0f;
    std::uint32_t blocked_ = 0; // buttons held since the start, ignored until let go
    bool blocked_logged_ = false;

    // Pages and focus.
    Page page_ = Page::home;
    float page_age_ = 0.0f; // seconds since the page appeared
    int page_direction_ = 1;
    int home_row_ = 0; // 0 the actions, 1 the pairing cards and the paired devices
    int home_action_ = 0;
    int home_card_ = 0; // the pairing cards, then the streams card (while someone streams), then the paired devices
    int tile_ = 0; // the option tiles, then the four actions under them
    float tile_amount_[12 + PS_EXP_SWITCH_COUNT + 1 + 4] = {};
    float action_amount_[3] = {};
    float card_amount_[2] = {};
    float devices_amount_ = 0.0f; // the paired devices' focus
    float streams_amount_ = 0.0f; // the streams card's focus
    int seen_devices_index_ = 0;  // devices_index() of the last frame
    float tab_x_ = -1.0f; // the selected tab's plate glides between tabs
    hui::ui::ComponentStyle style_;
    hui::ui::Highlight highlight_;
    bool snap_highlight_ = true;
    float sun_spin_ = 0.0f;
    float sun_glow_ = 0.0f;
    float sun_dim_ = 1.0f;
    float pulse_ = 0.0f; // the stream's rings

    // The report, split into lines for the diagnostics page.
    std::vector<std::string> report_lines_;
    std::uint64_t seen_report_ = 0;
    int report_scroll_ = 0;
    float report_amount_ = 0.0f; // its button's focus

    // The sheet.
    Sheet sheet_ = Sheet::none;
    Sheet sheet_drawn_ = Sheet::none; // stays while the sheet fades out
    float sheet_amount_ = 0.0f;
    std::string sheet_title_;
    std::string sheet_help_;
    std::string buffer_;
    std::string sheet_error_;
    std::string target_; // the pairing id or the option key
    std::string target_name_;
    std::string pair_pin_;        // the PIN while the device's name is asked after it
    std::string pair_suggestion_; // the name that takes when the field is left empty
    bool renaming_ = false;       // the text sheet renames the device target_
    WebStep web_step_ = WebStep::none; // the text sheet asks for the web page's user or password
    std::string web_user_;             // the user name while the password is asked after it
    bool show_password_ = false;       // R3: the password in clear while it is typed; off with every new sheet
    int web_row_ = 0;                  // the focused row of the web page's sheet
    int device_row_ = 0;          // the focused row of the device list or the stream list
    int device_top_ = 0;          // the first row in view
    int choice_ = 0; // the focused row of a choice sheet (0: the default)
    long number_ = 0;
    bool number_default_ = false;
    hui::ui::Keyboard keyboard_;
    hui::ui::Highlight sheet_highlight_;
};

} // namespace psapp
