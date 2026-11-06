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
std::string install(bool check_only, const std::filesystem::path &root = {});

}
