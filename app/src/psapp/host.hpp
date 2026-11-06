// SPDX-License-Identifier: GPL-3.0-only
// Everything the app does to the host, on one worker thread so the frame loop never waits:
//   - the host's control socket on 127.0.0.1:47995 (src/control/control_server.hpp): STATUS, PAIRINGS,
//     DEVICES and STREAMS every two seconds, PIN and CANCELPIN for a pending pairing, RENAME and UNPAIR for a paired
//     device, ENDSTREAM to end one device's stream, QUIT to stop it, REPORT to have it offer the diagnostic report on the network (a title may not
//     listen there itself), WEBLOGIN for the web page's login (web.hpp; the password never goes to a log);
//   - starting the host: its ELF sent to the ELF loader on 127.0.0.1:9021;
//   - /data/prosperosunshine/sunshine.conf (conf.hpp) and the Payload Manager's autoload list
//     (autoload.hpp).
// The screen reads a Snapshot each frame and queues requests; results come back as a message.
#pragma once

#include "app/psapp/conf.hpp"
#include "app/psapp/diag.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace psapp
{

constexpr const char *kDataDir = "/data/prosperosunshine";
constexpr const char *kConfPath = "/data/prosperosunshine/sunshine.conf";
constexpr const char *kHostElf = "/data/prosperosunshine/prospero_sunshine.elf";
constexpr const char *kStatePath = "/data/prosperosunshine/sunshine_state.json";
constexpr const char *kBundledElf = "/app0/payloads/prospero_sunshine.elf";
constexpr const char *kHostDiagPath = "/data/prosperosunshine/host_diag.log";
constexpr const char *kHostLogPath = "/data/prosperosunshine/sunshine.log";
constexpr const char *kReportPath = "/data/prosperosunshine/diagnostics.txt";
constexpr std::uint16_t kControlPort = 47995;
constexpr std::uint16_t kLoaderPort = 9021;
constexpr std::uint16_t kStreamPort = 47989;
constexpr std::uint16_t kReportPort = 47996;

struct Pairing
{
    std::string id;
    std::string name;
    std::string address;
};

using Values = std::map<std::string, std::optional<std::string>>; // key -> value, nullopt: default

struct Snapshot
{
    // The host, from the last poll.
    bool polled = false;      // at least one poll finished
    bool running = false;     // the control socket answered
    bool streaming = false;   // STATUS running=1
    std::string app;          // STATUS app=
    bool client_known = false; // the host reports client= (newer hosts); then empty means nobody
    std::string client;       // STATUS client= and address=
    std::string client_address;
    std::vector<Pairing> pairings;
    std::string poll_error;   // set when the socket is there but did not answer properly
    bool devices_known = false;       // the running host answered DEVICES (an older one does not)
    std::vector<diag::Device> devices;
    bool streams_known = false;       // the running host answered STREAMS (an older one does not)
    std::vector<diag::Stream> streams; // who streams now
    bool web_known = false;           // the running host answered WEBLOGIN (an older one does not)
    std::string web_user;             // the stored login's user, also while the page is off; empty: no login
    bool web_enabled = false;         // the page is on
    bool web_switchable = false;      // the host keeps the login while the page is off (ON, OFF, FORGET)
    // Files.
    bool files_read = false;
    Values config;            // the keys of option_specs()
    std::string config_error; // reading failed (a missing file is fine)
    bool autoload = false;           // local/prospero-sunshine is in autoload.txt
    bool manager_autoload = false;   // AUTOLOAD_ENABLED=1 in pldmgr_config.txt (true when unreadable)
    bool manager_present = false;    // /data/pldmgr exists
    long long host_elf_size = -1;    // /data/prosperosunshine/prospero_sunshine.elf
    std::vector<std::string> paired; // named_devices in sunshine_state.json
    std::string address;             // the console's IPv4 address, for Moonlight
    bool bundled_elf = false;        // the app carries one
    // Diagnostics.
    std::uint32_t firmware = 0;      // the console's, from the title or else from the host's file
    diag::Notice notice;             // the home page's line about firmware and capture
    std::uint64_t report_serial = 0; // grows with every new report; Host::report() has the text
    bool report_served = false;      // the host offers the report on the network
    // The request being worked on ("" when idle), and the outcome of the last one.
    std::string busy;
    std::string message;
    bool message_ok = true;
    std::uint64_t message_serial = 0; // grows with every new message
    std::uint64_t paired_serial = 0;  // grows with every successful PIN
};

class Host
{
  public:
    Host();
    ~Host();

    void start(); // starts the worker thread
    Snapshot snapshot();

    // Requests; each is queued and done in order on the worker.
    void submit_pin(const std::string &id, const std::string &pin, const std::string &name);
    void deny_pairing(const std::string &id, const std::string &name); // name: for the message
    void rename_device(const std::string &id, const std::string &name);
    void remove_device(const std::string &id, const std::string &name); // name: for the message
    void end_stream(const std::string &id, const std::string &name);    // the game and the others go on
    void set_web_login(const std::string &user, const std::string &password); // turns the web page on
    void web_login_on();                                                       // with the stored login
    void web_login_off();                                                      // keeps the login (an older host removes it)
    void web_login_forget();                                                   // removes the login
    void start_host();
    void stop_host();
    void restart_host();
    void save_config(const Values &values, bool restart);
    void set_autoload(bool on);
    void refresh();
    void make_report();         // writes kReportPath; the outcome comes as a message
    void serve_report(bool on); // while on and the host runs, the report is at http://<console>:kReportPort/
    void set_app_version(const std::string &version);
    std::string report();

  private:
    enum class Kind
    {
        pin,
        deny,
        rename_device,
        remove_device,
        end_stream,
        web_set,
        web_on,
        web_off,
        web_forget,
        start,
        stop,
        restart,
        save,
        autoload_on,
        autoload_off,
        refresh,
        report,
    };
    struct Job
    {
        Kind kind;
        std::string a, b, c;
        Values values;
        bool flag = false;
    };

    static void *thread_main(void *self);
    void run();
    void push(Job job);
    void set_busy(const std::string &text);
    void finish(const std::string &message, bool ok);

    void prepare_files();
    void prepare_through_host();
    bool same_as_bundled();
    bool bridged_ = false; // no /data here: a host answered, so its files were read. Worker thread only.
    void read_files();
    std::string build_report();
    void poll();
    bool wait_control(bool open, int timeout_ms);
    std::string launch();
    std::string quit_and_wait();
    std::string sync_slot();

    void do_pin(const Job &job);
    void do_deny(const Job &job);
    void do_rename_device(const Job &job);
    void do_remove_device(const Job &job);
    void do_end_stream(const Job &job);
    void do_web(const Job &job);
    void do_start();
    void do_stop();
    void do_restart();
    void do_save(const Job &job);
    void do_autoload(bool on);
    std::string autoload_direct(bool on);
    void do_report();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    Snapshot state_;
    bool stop_ = false;
    bool started_ = false;
    std::atomic<bool> serve_{false};
    std::string report_;
    std::string app_version_;
    std::uint32_t own_firmware_ = 0; // what the title itself could read
};

} // namespace psapp
