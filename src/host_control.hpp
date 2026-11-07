// SPDX-License-Identifier: GPL-3.0-only
// Stopping and starting the host from a payload on the console, through the ports the app uses: QUIT on the control
// socket (127.0.0.1:47995, answered "OK bye"), and the host's ELF to the ELF loader (127.0.0.1:9021). The firmware probe
// and host_restart.elf use it, so that nobody has to press Stop host and Start host in the app. The host uses send_elf for
// its control command PROBE, which starts the probe installed at kProbe.
// The ELF started is /data/prosperosunshine/prospero_sunshine.elf, the one the app installed, unless the one-line file
// /data/prosperosunshine/host_restart.next names another (a test build uploaded there), or says "none".
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace host_control {

inline constexpr uint16_t kStreamPort = 47989, kControlPort = 47995, kLoaderPort = 9021;
inline const char *const kInstalled = "/data/prosperosunshine/prospero_sunshine.elf";
inline const char *const kNextFile = "/data/prosperosunshine/host_restart.next";
inline const char *const kProbe = "/data/prosperosunshine/firmware_probe.elf";

inline int loopback(uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a)) {
        close(fd);
        return -1;
    }
    return fd;
}

inline bool listening(uint16_t port) {
    const int fd = loopback(port);
    if (fd < 0) return false;
    close(fd);
    return true;
}

inline bool wait_port(uint16_t port, bool open, int seconds) {
    for (int i = 0; i < seconds * 10; i++) {
        if (listening(port) == open) return true;
        usleep(100000);
    }
    return false;
}

inline bool running() { return listening(kStreamPort) || listening(kControlPort); }

// The host's answer to STATUS on its control socket, "" if it doesn't answer.
inline std::string status() {
    const int fd = loopback(kControlPort);
    if (fd < 0) return "";
    const char line[] = "STATUS\n";
    char reply[256] = {};
    if (write(fd, line, sizeof line - 1) == static_cast<ssize_t>(sizeof line - 1)) {
        timeval tv{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        const ssize_t n = read(fd, reply, sizeof reply - 1);
        if (n > 0) reply[n] = 0;
    }
    close(fd);
    return reply;
}

// STATUS carries the client's address while a stream runs, and "address= client=" while none does.
inline bool streaming_in(const std::string &status) {
    const size_t at = status.find(" address=");
    return at != std::string::npos && at + 9 < status.size() && status[at + 9] != ' ' && status[at + 9] != '\n' &&
           status[at + 9] != '\r';
}

// Whether the probe must not go on: a stream in STATUS, or no answer while the host listens. A host that is up but silent
// may be busy with a stream, and stopping it then would cut someone's stream, so that counts as busy too.
inline bool busy_in(const std::string &status, bool host_listening) {
    return status.empty() ? host_listening : streaming_in(status);
}

// STATUS asked up to three times, a second apart, since the control socket takes one client at a time. why says what
// was seen when busy.
inline bool busy(std::string &why) {
    std::string answer;
    for (int i = 0; i < 3 && answer.empty(); i++) {
        if (i) sleep(1);
        answer = status();
    }
    const bool listening_now = listening(kStreamPort);
    why = answer.empty() ? "the host listens but didn't answer STATUS" : "the host is streaming";
    return busy_in(answer, listening_now);
}

// Asks a running host to quit and waits for its control socket, which closes after the streams and their capture session.
// True if no host runs afterwards.
inline bool stop(std::string &detail) {
    if (!running()) {
        detail = "no host running";
        return true;
    }
    const int fd = loopback(kControlPort);
    if (fd < 0) {
        detail = "host listening, but its control socket isn't";
        return false;
    }
    const char quit[] = "QUIT\n";
    const bool sent = write(fd, quit, sizeof quit - 1) == static_cast<ssize_t>(sizeof quit - 1);
    char reply[64] = {};
    if (sent) {
        timeval tv{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        const ssize_t n = read(fd, reply, sizeof reply - 1);
        if (n > 0) reply[n] = 0;
    }
    close(fd);
    for (char *c = reply; *c; c++)
        if (*c == '\n' || *c == '\r') *c = ' ';
    const bool down = sent && wait_port(kStreamPort, false, 15) && wait_port(kControlPort, false, 20);
    if (down) usleep(2000000);  // the pads and the rest of the process, as the app waits too (Host::quit_and_wait)
    detail = std::string("QUIT answered \"") + reply + "\", " + (down ? "host stopped" : "host still listening after 35 s");
    return down;
}

// The ELF to start: kNextFile's path, else the installed one; "" for "none".
inline std::string next_elf() {
    std::ifstream in(kNextFile);
    std::string path;
    std::getline(in, path);
    while (!path.empty() && (path.back() == '\r' || path.back() == ' ')) path.pop_back();
    if (path == "none") return "";
    return path.empty() ? kInstalled : path;
}

// Sends the ELF at path to the loader. The bytes sent, or 0 with detail saying why not.
inline size_t send_elf(const std::string &path, std::string &detail) {
    std::ifstream in(path, std::ios::binary);
    const std::vector<char> elf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (elf.empty()) {
        detail = path + " not found";
        return 0;
    }
    const int fd = loopback(kLoaderPort);
    if (fd < 0) {
        detail = "no ELF loader on 9021";
        return 0;
    }
    // In the host this runs on the control socket's thread: a loader that closes early must not raise SIGPIPE, and one
    // that accepts but doesn't read must not block QUIT and RELISTEN behind it.
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    timeval timeout{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    size_t done = 0;
    while (done < elf.size()) {
        const ssize_t n = write(fd, elf.data() + done, elf.size() - done);
        if (n <= 0) break;
        done += static_cast<size_t>(n);
    }
    close(fd);
    if (done != elf.size()) {
        detail = path + ": the loader took " + std::to_string(done) + " of " + std::to_string(elf.size()) + " bytes";
        return 0;
    }
    return done;
}

// Sends the ELF at path to the loader and waits for the host's stream port.
inline bool start(const std::string &path, std::string &detail) {
    const size_t size = send_elf(path, detail);
    if (!size) return false;
    const bool up = wait_port(kStreamPort, true, 30);
    detail = path + " (" + std::to_string(size) + " bytes) sent, " + (up ? "host listening" : "host NOT listening after 30 s");
    return up;
}

}  // namespace host_control
