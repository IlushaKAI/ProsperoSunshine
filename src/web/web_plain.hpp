// SPDX-License-Identifier: GPL-3.0-only
// The web page speaks only HTTPS. A browser sent to http://<console>:<port> got its connection closed and showed an empty
// response (issue 31). The server looks at a connection's first byte: a TLS handshake starts with 0x16, a plain request
// with a method's letter, which gets the answer built here. Portable, tested on the PC (tests/web_plain_test.cpp).
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace prospero::web {

// Whether a connection's first byte starts a TLS handshake (a TLS record of type handshake).
inline bool starts_tls(unsigned char first) { return first == 0x16; }

// The host part of a Host header without its port, when it is a plain name, an IPv4 address or a bracketed IPv6 address.
// Empty for anything else, so nothing the client sent reaches the answer unchecked.
inline std::string plain_host(std::string_view host) {
    while (!host.empty() && (host.front() == ' ' || host.front() == '\t')) host.remove_prefix(1);
    while (!host.empty() && (host.back() == ' ' || host.back() == '\t' || host.back() == '\r')) host.remove_suffix(1);
    if (host.empty() || host.size() > 255) return {};
    if (host.front() == '[') {
        const size_t close = host.find(']');
        if (close == std::string_view::npos) return {};
        const std::string_view inner = host.substr(1, close - 1);
        if (inner.empty()) return {};
        for (const char c : inner)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':' || c == '.'))
                return {};
        const std::string_view rest = host.substr(close + 1);
        if (!rest.empty() && rest.front() != ':') return {};
        return std::string(host.substr(0, close + 1));
    }
    const size_t colon = host.find(':');
    const std::string_view name = host.substr(0, colon);
    if (name.empty()) return {};
    for (const char c : name)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-')) return {};
    return std::string(name);
}

// The Host header's value in a request head ("GET / HTTP/1.1\r\nHost: ...\r\n..."), or empty.
inline std::string_view host_header(std::string_view head) {
    size_t at = head.find("\r\n");
    while (at != std::string_view::npos && at + 2 < head.size()) {
        const size_t start = at + 2;
        const size_t end = head.find("\r\n", start);
        const std::string_view line = head.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.empty()) break;
        if (line.size() >= 5) {
            bool match = true;
            for (size_t i = 0; i < 5 && match; ++i) match = (line[i] | 0x20) == "host:"[i];
            if (match) return line.substr(5);
        }
        at = end;
    }
    return {};
}

// The answer to a plain HTTP request on the page's port: a redirect to https:// on the same host and port, with a short
// page for clients that don't follow it. Without a usable Host header, the page only says to use https://.
inline std::string plain_http_answer(std::string_view head, uint16_t port) {
    const std::string host = plain_host(host_header(head));
    std::string body = "<!doctype html><meta charset=\"utf-8\"><title>ProsperoSunshine</title>"
                       "<p>The ProsperoSunshine web page needs <b>https://</b>";
    std::string answer;
    if (host.empty()) {
        body += ": open https://&lt;console-ip&gt;:" + std::to_string(port) + " instead.</p>\n";
        answer = "HTTP/1.1 400 Bad Request\r\n";
    } else {
        const std::string url = "https://" + host + ":" + std::to_string(port) + "/";
        body += ": <a href=\"" + url + "\">" + url + "</a></p>\n";
        answer = "HTTP/1.1 302 Found\r\nLocation: " + url + "\r\n";
    }
    answer += "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
              std::to_string(body.size()) + "\r\n\r\n" + body;
    return answer;
}

}  // namespace prospero::web
