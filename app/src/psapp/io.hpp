// SPDX-License-Identifier: GPL-3.0-only
// Files and loopback sockets. Everything here blocks, so only the worker thread (host.hpp) calls it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psapp::io
{

// ---- files ----
// Where a title has no /data (9.60), paths under /data go through the host instead: the files src/control/file_bridge.hpp
// lists, while the host runs. Then copy_file and same_content don't reach them, and send_file never does.
bool sandboxed();
bool read_text(const std::string &path, std::string *out, std::size_t max_bytes = 1u << 20);
// Writes path.tmp, then renames it over path. Returns an error text, empty on success.
std::string write_text(const std::string &path, const std::string &data);
// Copies src to dst through dst.tmp and a rename. Returns an error text, empty on success.
std::string copy_file(const std::string &src, const std::string &dst);
// Size of a regular file, or -1.
long long file_size(const std::string &path);
// Byte for byte the same (both must exist).
bool same_content(const std::string &a, const std::string &b);
bool exists(const std::string &path);
// mkdir -p for an absolute path.
bool make_dirs(const std::string &path);
// Why the file does not open for reading ("errno 13 (permission denied)"), empty when it does.
std::string open_error(const std::string &path);
// One line on a path for the diagnostic report: what it is, its owner and mode, and whether this title
// may read and write it. Leaves nothing behind.
std::string describe_path(const std::string &path);
// The console's firmware (kern.sdk_version, as 0x13000040), or 0 when the title may not ask.
std::uint32_t firmware_version();

// ---- loopback TCP ----
// Connects to 127.0.0.1:port, sends `request` and reads lines until `last_line` returns true for
// one (or the peer closes). Returns an error text, empty on success; `connect_refused` tells "nobody
// listens" apart from other failures.
struct Exchange
{
    std::vector<std::string> lines;
    std::string error;
    bool connect_refused = false;
};
Exchange exchange(std::uint16_t port, const std::string &request, bool (*last_line)(const std::string &),
                  int timeout_ms);
// True when something accepts a connection on 127.0.0.1:port.
bool port_open(std::uint16_t port, int timeout_ms = 500);
// The address of the interface that routes to the LAN (a UDP socket "connected" to a private
// address, which sends nothing), as dotted IPv4; empty when there is none.
std::string local_ipv4();
// Sends a file's bytes to 127.0.0.1:port and closes (the ELF loader's protocol). Error text or empty.
std::string send_file(std::uint16_t port, const std::string &path);

} // namespace psapp::io
