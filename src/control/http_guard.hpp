// SPDX-License-Identifier: GPL-3.0-only
// The control socket runs one command per line. A web page in a browser can't speak that, but it can send an HTTP request
// to 127.0.0.1:47995 (or to the console's address with control_lan), and a POST's body lines would run as commands, for
// example WEBLOGIN SET. The socket closes the connection on a line that starts an HTTP request. Portable, tested on the PC
// (tests/http_guard_test.cpp).
#pragma once
#include <string_view>

namespace prospero::control {

// Whether a line is an HTTP request line: "<METHOD> <target> HTTP/<digit>...", the first line every browser sends. Only
// "RENAME <uuid> HTTP/1.1" of the socket's own commands looks the same; a device can't be given that name over the socket.
inline bool http_request_line(std::string_view line) {
    const size_t first = line.find(' ');
    if (first == 0 || first == std::string_view::npos) return false;
    for (const char c : line.substr(0, first))
        if (c < 'A' || c > 'Z') return false;
    const size_t last = line.rfind(' ');
    if (last == first || line.find(' ', first + 1) != last) return false;
    const std::string_view version = line.substr(last + 1);
    return version.size() >= 6 && version.substr(0, 5) == "HTTP/" && version[5] >= '0' && version[5] <= '9';
}

}  // namespace prospero::control
