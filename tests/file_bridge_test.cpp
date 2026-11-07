// SPDX-License-Identifier: GPL-3.0-only
// What the app may reach through the host where a title has no /data: the listed files and no others.
#include "control/file_bridge.hpp"
#include "../app/src/psapp/file_wire.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace prospero::file_bridge;
using psapp::file_wire::from_hex;
using psapp::file_wire::to_hex;

static void put(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

static std::string get(const fs::path &path) {
    std::ostringstream text;
    text << std::ifstream(path, std::ios::binary).rdbuf();
    return text.str();
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("file_bridge_test." + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path data = root / "data/prosperosunshine";
    fs::create_directories(data);
    const std::string binary("a\0\xff\n b", 6);

    std::string back;
    assert(from_hex(to_hex(binary), &back) && back == binary);
    assert(to_hex("").empty() && from_hex("", &back) && back.empty());
    assert(!from_hex("abc", &back) && !from_hex("zz", &back) && !from_hex("AB", &back));

    // Reading.
    assert(file("STAT /data/prosperosunshine/sunshine.conf", root) == "ERR missing");
    assert(file("GET /data/prosperosunshine/sunshine.conf 100", root) == "ERR missing");
    put(data / "sunshine.conf", binary);
    assert(file("STAT /data/prosperosunshine", root).starts_with("OK dir 0 "));
    assert(file("STAT /data/prosperosunshine/sunshine.conf", root).starts_with("OK file 6 "));
    assert(file("GET /data/prosperosunshine/sunshine.conf 100", root) == "OK " + to_hex(binary));
    assert(file("GET /data/prosperosunshine/sunshine.conf 5", root) == "ERR too large");
    put(data / "host_diag.log", "");
    assert(file("GET /data/prosperosunshine/host_diag.log 100", root) == "OK ");

    // Writing, through a temporary file.
    assert(file("PUT /data/prosperosunshine/sunshine.conf " + to_hex("new"), root) == "OK");
    assert(get(data / "sunshine.conf") == "new" && !fs::exists(data / "sunshine.conf.tmp"));
    assert(file("PUT /data/prosperosunshine/sunshine.conf", root) == "OK" && get(data / "sunshine.conf").empty());
    assert(file("PUT /data/prosperosunshine/sunshine.conf xy", root) == "ERR not hex");
    // Keys that would point the host at other files are refused, comments and other keys pass.
    const std::string conf_before = get(data / "sunshine.conf");
    for (const char *line : {"log_path = /data/x", "  cert=/data/c.pem", "file_state = /tmp/s", "credentials_file=x", "pkey = k",
                             "file_apps = a.json", "global_prep_cmd = []"})
        assert(file("PUT /data/prosperosunshine/sunshine.conf " + to_hex(std::string("hevc_mode = 1\n") + line + "\n"), root)
                   .starts_with("ERR not allowed: "));
    assert(get(data / "sunshine.conf") == conf_before);
    assert(path_key("# log_path = x\nmin_log_level = debug\nexperimental_hevc = 1\ncertificate_note = 1\n").empty());
    assert(path_key("cert = x") == "cert" && path_key("\tlog_path=x") == "log_path");
    assert(file("PUT /data/prosperosunshine/diagnostics.txt " + to_hex("log_path = x\n"), root) == "OK");

    // Only the app's socket on this console: not the web page, not a client over the LAN switch.
    assert(allowed(false, true) && !allowed(true, true) && !allowed(false, false) && !allowed(true, false));

    // Only what is listed: not the keys, not around the list, and nothing the console would run.
    put(data / "credentials/cakey.pem", "secret");
    for (const char *path : {"/data/prosperosunshine/credentials/cakey.pem", "/data/prosperosunshine/../prosperosunshine/sunshine.conf",
                             "/data/prosperosunshine/sunshine.conf/", "/etc/passwd", "/data", "sunshine.conf", ""}) {
        assert(file("STAT " + std::string(path), root).starts_with("ERR"));
        assert(file("GET " + std::string(path) + " 100", root).starts_with("ERR"));
    }
    for (const char *path : {"/data/prosperosunshine/prospero_sunshine.elf", "/data/pldmgr/autoload.txt", "/data/prosperosunshine/control_lan",
                             "/data/prosperosunshine/credentials/cakey.pem"})
        assert(file("PUT " + std::string(path) + ' ' + to_hex("x"), root) == "ERR denied");
    assert(!fs::exists(data / "prospero_sunshine.elf") && get(data / "credentials/cakey.pem") == "secret");
    assert(file("DELETE /data/prosperosunshine/sunshine.conf", root).starts_with("ERR usage"));

    // The app's host.
    assert(install(false, root) == "ERR the app carries no host");
    put(root / "data/homebrew/PPSA47989/payloads/prospero_sunshine.elf", "new host");
    assert(install(true, root) == "OK differs" && !fs::exists(data / "prospero_sunshine.elf"));
    assert(install(false, root) == "OK installed" && get(data / "prospero_sunshine.elf") == "new host");
    assert(install(false, root) == "OK same" && install(true, root) == "OK same");

    fs::remove_all(root);
    puts("PASS: the host's files for the app (listed files only, writes through a temporary file, the app's host)");
    return 0;
}
