// SPDX-License-Identifier: GPL-3.0-only
// File contents on the control socket, whose requests and answers are lines: hex, for the app (io.cpp) and the host
// (src/control/file_bridge.cpp) alike.
#pragma once

#include <string>
#include <string_view>

namespace psapp::file_wire
{

inline std::string to_hex(std::string_view bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const char c : bytes)
    {
        const auto byte = static_cast<unsigned char>(c);
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

inline bool from_hex(std::string_view text, std::string *bytes)
{
    const auto value = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    if (text.size() % 2 != 0)
        return false;
    bytes->clear();
    bytes->reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2)
    {
        const int high = value(text[i]), low = value(text[i + 1]);
        if (high < 0 || low < 0)
            return false;
        *bytes += static_cast<char>(high << 4 | low);
    }
    return true;
}

} // namespace psapp::file_wire
