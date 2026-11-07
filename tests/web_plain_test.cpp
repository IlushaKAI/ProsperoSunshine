// SPDX-License-Identifier: GPL-3.0-only
#include "web/web_plain.hpp"
#include <cassert>
#include <iostream>
using namespace prospero::web;

int main() {
    assert(starts_tls(0x16) && !starts_tls('G') && !starts_tls(0));

    assert(plain_host("192.168.1.5:48990") == "192.168.1.5");
    assert(plain_host(" ps5.local ") == "ps5.local");
    assert(plain_host("[fe80::1]:47990") == "[fe80::1]");
    assert(plain_host("[fe80::1]") == "[fe80::1]");
    assert(plain_host("").empty() && plain_host(":47990").empty() && plain_host("[]").empty());
    assert(plain_host("evil\"><script>").empty() && plain_host("a b").empty() && plain_host("[fe80::1]x").empty());
    assert(plain_host("[fe80::1").empty() && plain_host("[g::1]").empty());

    assert(host_header("GET / HTTP/1.1\r\nUser-Agent: x\r\nHost: 192.168.1.5:48990\r\n\r\n") == " 192.168.1.5:48990");
    assert(host_header("GET / HTTP/1.1\r\nhOsT:ps5\r\n\r\n") == "ps5");
    assert(host_header("GET / HTTP/1.1\r\n\r\nHost: late\r\n").empty());
    assert(host_header("GET / HTTP/1.0\r\n\r\n").empty() && host_header("").empty());

    const std::string moved = plain_http_answer("GET /x HTTP/1.1\r\nHost: 192.168.1.5:48990\r\n\r\n", 48990);
    assert(moved.rfind("HTTP/1.1 302 Found\r\nLocation: https://192.168.1.5:48990/\r\n", 0) == 0);
    assert(moved.find("Connection: close") != std::string::npos);
    const size_t split = moved.find("\r\n\r\n");
    const size_t length = std::stoul(moved.substr(moved.find("Content-Length: ") + 16));
    assert(split != std::string::npos && moved.size() - split - 4 == length);

    const std::string ipv6 = plain_http_answer("GET / HTTP/1.1\r\nHost: [fe80::1]:47990\r\n\r\n", 47990);
    assert(ipv6.find("Location: https://[fe80::1]:47990/\r\n") != std::string::npos);

    const std::string unknown = plain_http_answer("GET / HTTP/1.1\r\nHost: <bad>\r\n\r\n", 47990);
    assert(unknown.rfind("HTTP/1.1 400 Bad Request\r\n", 0) == 0 && unknown.find("Location") == std::string::npos);
    assert(unknown.find("<bad>") == std::string::npos && unknown.find("47990") != std::string::npos);

    std::cout << "PASS: plain HTTP on the web page's port (TLS byte, Host header, redirect, nothing unchecked echoed)\n";
}
