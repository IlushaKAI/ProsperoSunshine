// SPDX-License-Identifier: GPL-3.0-only
#include "control/http_guard.hpp"
#include <cassert>
#include <iostream>
using namespace prospero::control;

int main() {
    assert(http_request_line("POST / HTTP/1.1"));
    assert(http_request_line("GET /status?x=1 HTTP/1.0"));
    assert(http_request_line("OPTIONS * HTTP/1.1"));
    assert(http_request_line("PRI * HTTP/2.0"));
    // The socket's own commands, also with names that hold spaces or "HTTP/".
    assert(!http_request_line("STATUS"));
    assert(!http_request_line("PIN 1234"));
    assert(!http_request_line("RENAME ABC-1 Living room PC"));
    assert(!http_request_line("RENAME ABC-1 HTTP/server"));
    assert(!http_request_line("RENAME ABC-1 my HTTP/1.1"));
    assert(!http_request_line("GET / x HTTP/1.1"));
    assert(!http_request_line("WEBLOGIN SET admin password123"));
    assert(!http_request_line(""));
    assert(!http_request_line(" GET / HTTP/1.1"));
    assert(!http_request_line("get / HTTP/1.1"));
    assert(!http_request_line("GET HTTP/1.1"));
    std::cout << "PASS: control socket HTTP guard (request lines from browsers, never the socket's own commands)\n";
}
