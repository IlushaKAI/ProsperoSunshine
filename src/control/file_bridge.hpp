// SPDX-License-Identifier: GPL-3.0-only
// The host's files for the app where a title has no /data (firmware 9.60; ps5-homebrew-ui says the same of a sandbox). The
// host, a payload, sees them. The socket has no authentication, so this is a list of named files and not a way into /data:
// the credentials stay out, and nothing here can write an ELF that the console would run, other than the app's own host.
// Requests, after FILE:
//   STAT <path>        -> "OK file <size> <mode>" or "OK dir 0 <mode>" (mode in octal), "ERR missing", "ERR denied"
//   GET <path> <max>   -> "OK <hex>", "ERR too large", ...
//   PUT <path> <hex>   -> "OK", ...
// INSTALL copies the host ELF of the app's install folder to the data folder, unless it's there: "OK same", "OK installed".
// INSTALL CHECK only compares: "OK same", "OK differs". `root` goes in front of every path, for tests.
#pragma once
#include <filesystem>
#include <string>

namespace prospero::file_bridge {

std::string file(const std::string &request, const std::filesystem::path &root = {});

// Who may use FILE and INSTALL: the app's socket (not the web page) from this console (not over the LAN switch).
inline bool allowed(bool web_caller, bool local_client) { return !web_caller && local_client; }

// The first key of a sunshine.conf text that points the host at another file (certificate, key, state, log, app list), or
// empty. The app never writes those, and a FILE PUT with one is refused: written there, they could make the host read or
// overwrite files outside the list above.
std::string path_key(const std::string &conf);
std::string install(bool check_only, const std::filesystem::path &root = {});

}
