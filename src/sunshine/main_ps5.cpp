// SPDX-License-Identifier: GPL-3.0-only
// Host entry point; replaces Sunshine's main.cpp/entry_handler.cpp. Starts the GameStream HTTP(S)
// and RTSP services with the PS5 platform layer and offers a small line-based control socket for
// development (ROADMAP P2.3): pairing PIN, status and shutdown.
#include "apps/titles.hpp"
#include "control/control_server.hpp"
#include "control/listener_watch.hpp"
#include "input/title_profile.hpp"
#include "platform/ps5/mdns_responder.hpp"
#include "web/web_server.hpp"
#include "platform/ps5/capture.h"
#include "platform/ps5/experimental.h"
#include "src/config.h"
#include "src/file_handler.h"
#include "src/globals.h"
#include "src/httpcommon.h"
#include "src/input.h"
#include "src/logging.h"
#include "src/network.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include "src/process.h"
#include "src/rtsp.h"
#include "src/video.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <boost/core/null_deleter.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <rs.h>
#include <thread>
extern "C" {
#include <ps5/kernel.h>
#include <ps5/klog.h>
#include "platform/ps5/diag.h"
#include "platform/ps5/ra_nids.h"
int sceKernelSetProcessName(const char *name);
}

using namespace std::literals;

namespace {
// Sunshine logs to stdout and a file; on the console the kernel log is what developers watch.
struct klog_backend : boost::log::sinks::basic_formatted_sink_backend<char, boost::log::sinks::synchronized_feeding> {
    void consume(const boost::log::record_view &, const std::string &text) {
        klog_printf("ProsperoSunshine: host %s\n", text.c_str());
    }
};

using klog_sink = boost::log::sinks::synchronous_sink<klog_backend>;

boost::shared_ptr<klog_sink> add_klog_sink() {
    namespace expr = boost::log::expressions;
    auto sink = boost::make_shared<klog_sink>();
    sink->set_formatter(expr::stream << '[' << expr::attr<int>("Severity") << "] " << expr::smessage);
    sink->set_filter(expr::attr<int>("Severity") >= 2);  // info and above until the config is read
    boost::log::core::get()->add_sink(sink);
    return sink;
}

using Ports = std::array<uint16_t, 3>;

// Returns false when the host was told to stop, true when rest mode has taken its ports: the process runs on, but
// nothing answers until the services start anew (STATUS.md). RELISTEN on the control socket tests that path.
bool wait_or_ports_lost(const std::shared_ptr<safe::event_t<bool>> &shutdown_event, const Ports &ports,
                        prospero::ControlServer &control) {
    bool up = false;
    int refused = 0;
    while (!shutdown_event->view(5s)) {
        if (control.take_relisten()) return true;
        if (std::none_of(ports.begin(), ports.end(), prospero::port_refused)) {
            up = true;
            refused = 0;
        } else if (up && ++refused >= 2) {
            return true;
        }
    }
    return false;
}

// The keyboard pad's own list (input/title_profile.hpp): the installed titles it doesn't mention yet go in as commented
// lines with the profile the title map gives them, so the player picks one by removing a #. The player's lines stay.
void note_installed_titles(const std::filesystem::path &file) {
    std::vector<prospero::ListedTitle> listed;
    for (const auto &title : prospero::list_titles("/user")) {
        const auto meta = prospero::read_title_meta("/user", title.id);
        const auto choice = prospero::choose_profile(title.id, meta ? meta->concept_id : 0, meta ? meta->english : title.name,
                                                     {}, prospero::Profile::standard);
        const bool mapped = choice.source != prospero::ProfileSource::chosen;
        listed.push_back({title.id, title.name, mapped ? std::optional{choice.profile} : std::nullopt});
    }
    const auto added = prospero::profile_list_additions(file_handler::read_file(file.c_str()), listed);
    if (added.empty()) return;
    std::ofstream(file, std::ios::app) << added;
    BOOST_LOG(info) << "keyboard_profiles.txt: noted the installed titles it didn't mention"sv;
}
}

int main(int argc, char *argv[]) {
    // The ELF loader names every payload "payload.elf"; this is what the Payload Manager's process list shows (issue
    // 32). The kernel keeps at most 19 characters, so not "prospero_sunshine.elf".
    const int name_rc = sceKernelSetProcessName("ProsperoSunshine");
    const auto klog = add_klog_sink();
    std::error_code ec;
    std::filesystem::create_directories(platf::appdata(), ec);
    // Before anything that can fail: the app's diagnostic report reads this file, also after a crash.
    const uint32_t firmware = kernel_get_fw_version();
    char header[160];
    std::snprintf(header, sizeof header, "ProsperoSunshine: host version=%s commit=%s firmware=%#x known=%d", PROJECT_VERSION,
                  PROJECT_VERSION_COMMIT, firmware,
                  ps_ra_find_known(firmware) != nullptr);
    ps_diag_open((platf::appdata() / "host_diag.log").c_str(), header);
    ps_diag_log("ProsperoSunshine: host appdata=%s mkdir_ec=%d config=%s\n", platf::appdata().c_str(), ec.value(),
                config::sunshine.config_file.c_str());
    if (name_rc != 0) ps_diag_log("ProsperoSunshine: host process name not set rc=%#x\n", name_rc);
    // config::parse copies a template apps.json from Sunshine's assets when none exists; there are
    // no assets on the console, and the app list is fixed in process_ps5.cpp anyway.
    if (const auto apps = platf::appdata() / "apps.json"; !std::filesystem::exists(apps, ec))
        std::ofstream(apps) << R"({"env":{},"apps":[{"name":"PS5 Screen"}]})" << '\n';
    // Held controller input stays active until a dead client is detected. Moonlight pings the
    // control stream every 100 ms, so 2 s (upstream default 10 s) still tolerates 20 lost pings while
    // cutting the stuck-input window after a network drop (P4.3). sunshine.conf can override it.
    config::stream.ping_timeout = 2s;
    // Moonlight's on-screen layouts have no PS button: holding Select this long sends it (gamepad_update
    // in platform_ps5.cpp). sunshine.conf can override it; a negative value turns it off.
    config::input.back_button_timeout = 500ms;
    mail::man = std::make_shared<safe::mail_raw_t>();
    if (const int rc = config::parse(argc, argv)) {
        ps_diag_log("ProsperoSunshine: host config::parse returned %d\n", rc);
        return rc < 0 ? 1 : 0;
    }
    // From here on the kernel log follows min_log_level from sunshine.conf like the file log.
    klog->set_filter(boost::log::expressions::attr<int>("Severity") >= config::sunshine.min_log_level);
    auto log_guard = logging::init(config::sunshine.min_log_level, config::sunshine.log_file);
    if (!log_guard) ps_diag_log("ProsperoSunshine: host logging failed to initialize\n");
    BOOST_LOG(info) << "ProsperoSunshine host starting, data in "sv << platf::appdata().string();
    if (config::ps5.close_game_on_quit) {
        BOOST_LOG(info) << "Terminate App in Moonlight closes the session's game (close_game_on_quit)"sv;
        ps_diag_log("ProsperoSunshine: host close_game_on_quit=1\n");
    }
    // The app's Experimental switches. Sunshine doesn't know these keys and leaves them alone.
    const auto settings = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));
    const auto switched_on = [&](const char *key) {
        const auto it = settings.find(key);
        return it != settings.end() && it->second == "1";
    };
    int switches[PS_EXP_SWITCH_COUNT];
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; ++i) switches[i] = switched_on(ps_exp_switch_at(i)->key);
    ps_capture_set_experimental(switches, (platf::appdata() / "experimental_attempt").c_str());

    task_pool.start(1);
    auto shutdown_event = mail::man->event<bool>(mail::shutdown);

    auto platform_guard = platf::init();
    auto proc_guard = proc::init();
    reed_solomon_init();
    auto input_guard = input::init();
    if (const auto it = settings.find("keyboard_mouse_pad"); prospero::keyboard_pad_enabled(it == settings.end() ? "" : it->second))
        note_installed_titles(std::filesystem::path(config::sunshine.config_file).parent_path() / "keyboard_profiles.txt");
    if (video::probe_encoders()) {
        ps_diag_log("ProsperoSunshine: host video capture unavailable\n");
        BOOST_LOG(fatal) << "Video capture unavailable"sv;
        return 1;
    }
    if (http::init()) {
        ps_diag_log("ProsperoSunshine: host HTTP interface failed to initialize\n");
        BOOST_LOG(fatal) << "HTTP interface failed to initialize"sv;
        return 1;
    }

    const Ports ports{net::map_port(nvhttp::PORT_HTTP), net::map_port(nvhttp::PORT_HTTPS),
                      net::map_port(rtsp_stream::RTSP_SETUP_PORT)};
    prospero::ControlServer control(shutdown_event);
    prospero::web::WebServer web(control, shutdown_event);
    for (bool again = true; again;) {
        std::jthread http_thread{nvhttp::start};
        std::jthread rtsp_thread{rtsp_stream::start};
        std::jthread control_thread([&](std::stop_token stop) { control.run(stop); });
        std::jthread report_thread([&](std::stop_token stop) { control.run_report(stop); });
        std::jthread web_thread([&](std::stop_token stop) { web.run(stop); });
        // Moonlight discovery (P9.2); on any socket problem the responder logs and ends, the host runs on.
        std::jthread mdns_thread([name = config::nvhttp.sunshine_name](std::stop_token stop) {
            prospero::mdns::run_responder(stop, name, net::map_port(nvhttp::PORT_HTTP));
        });
        ps_diag_log("ProsperoSunshine: host ready control_port=%u\n", prospero::ControlServer::port);

        again = wait_or_ports_lost(shutdown_event, ports, control);
        if (again) {
            // The log files stop taking lines after rest mode; the old sunshine.log stays as its rotated copy.
            ps_diag_reopen();
            log_guard.reset();
            log_guard = logging::init(config::sunshine.min_log_level, config::sunshine.log_file);
            ps_diag_log("ProsperoSunshine: host starting the services anew\n");
            BOOST_LOG(warning) << "Starting the services anew"sv;
            shutdown_event->raise(true);
        } else {
            BOOST_LOG(info) << "ProsperoSunshine host shutting down"sv;
        }
        control_thread.request_stop();
        report_thread.request_stop();
        web_thread.request_stop();
        mdns_thread.request_stop();
        proc::proc.terminate();
        mdns_thread.join();
        http_thread.join();
        rtsp_thread.join();
        control_thread.join();
        report_thread.join();
        web_thread.join();
        if (again) {
            shutdown_event->reset();
            while (!std::all_of(ports.begin(), ports.end(), prospero::port_free)) std::this_thread::sleep_for(2s);
        }
    }
    task_pool.stop();
    task_pool.join();
    ps_diag_log("ProsperoSunshine: host stopped\n");
    return 0;
}
