// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/io.hpp"

#include "app/psapp/diag.hpp"
#include "app/psapp/file_wire.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

namespace psapp::io
{

namespace
{

std::string errno_text(const char *what)
{
    return std::string(what) + " failed (errno " + std::to_string(errno) + ")";
}

long long now_ms()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<long long>(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}

bool write_all(int fd, const char *data, std::size_t size)
{
    while (size > 0)
    {
        const ssize_t n = ::write(fd, data, size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

// A connected loopback socket, or -1 (errno ECONNREFUSED when nobody listens).
int connect_loopback(std::uint16_t port, int timeout_ms)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
#ifdef SO_NOSIGPIPE
    // A peer that closes early must not end the title with SIGPIPE.
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0)
    {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK && errno != EINTR)
        {
            const int saved = errno;
            ::close(fd);
            errno = saved;
            return -1;
        }
        pollfd p{fd, POLLOUT, 0};
        if (::poll(&p, 1, timeout_ms) <= 0)
        {
            ::close(fd);
            errno = ETIMEDOUT;
            return -1;
        }
        int error = 0;
        socklen_t length = sizeof error;
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0)
        {
            ::close(fd);
            errno = error != 0 ? error : ECONNREFUSED;
            return -1;
        }
    }
    ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    return fd;
}

constexpr std::uint16_t kHostPort = 47995;

bool here(const std::string &path)
{
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0;
}

bool through_host(const std::string &path)
{
    return path.rfind("/data/", 0) == 0 && sandboxed();
}

// The host's one-line answer, or what stood in its way.
std::string ask_host(const std::string &request, int timeout_ms)
{
    const Exchange reply = exchange(kHostPort, request + "\n", [](const std::string &) { return true; }, timeout_ms);
    if (!reply.lines.empty())
        return reply.lines.front();
    return reply.connect_refused ? "ERR the host is not running" : "ERR " + reply.error;
}

// "OK file <size> <mode>" or "OK dir 0 <mode>", split up. False with the host's answer in *answer otherwise.
bool host_stat(const std::string &path, bool *folder, long long *size, std::string *mode, std::string *answer)
{
    *answer = ask_host("FILE STAT " + path, 3000);
    char kind[8] = {0}, bits[8] = {0};
    long long bytes = 0;
    if (std::sscanf(answer->c_str(), "OK %7s %lld %7s", kind, &bytes, bits) != 3)
        return false;
    *folder = std::strcmp(kind, "dir") == 0;
    *size = bytes;
    *mode = bits;
    return true;
}

} // namespace

bool sandboxed()
{
    static const bool value = !here("/data/homebrew") && !here("/data/prosperosunshine");
    return value;
}

bool read_text(const std::string &path, std::string *out, std::size_t max_bytes)
{
    out->clear();
    if (through_host(path))
    {
        const std::string answer = ask_host("FILE GET " + path + " " + std::to_string(max_bytes), 15000);
        if (answer.rfind("OK", 0) != 0)
        {
            errno = answer == "ERR missing" ? ENOENT : EIO;
            return false;
        }
        return file_wire::from_hex(answer.size() > 3 ? std::string_view(answer).substr(3) : std::string_view(), out);
    }
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof buffer, file)) > 0)
    {
        out->append(buffer, n);
        if (out->size() > max_bytes)
        {
            std::fclose(file);
            return false;
        }
    }
    const bool ok = !std::ferror(file);
    std::fclose(file);
    return ok;
}

std::string write_text(const std::string &path, const std::string &data)
{
    if (through_host(path))
    {
        const std::string answer = ask_host("FILE PUT " + path + " " + file_wire::to_hex(data), 15000);
        return answer == "OK" ? std::string() : answer;
    }
    const std::string temporary = path + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return errno_text(("open " + temporary).c_str());
    if (!write_all(fd, data.data(), data.size()))
    {
        const std::string error = errno_text("write");
        ::close(fd);
        ::unlink(temporary.c_str());
        return error;
    }
    ::fsync(fd);
    ::close(fd);
    if (::rename(temporary.c_str(), path.c_str()) != 0)
    {
        const std::string error = errno_text(("rename to " + path).c_str());
        ::unlink(temporary.c_str());
        return error;
    }
    return {};
}

std::string copy_file(const std::string &src, const std::string &dst)
{
    const int in = ::open(src.c_str(), O_RDONLY);
    if (in < 0)
        return errno_text(("open " + src).c_str());
    const std::string temporary = dst + ".tmp";
    const int out = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (out < 0)
    {
        const std::string error = errno_text(("open " + temporary).c_str());
        ::close(in);
        return error;
    }
    std::vector<char> buffer(1 << 20);
    std::string error;
    for (;;)
    {
        const ssize_t n = ::read(in, buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
        {
            error = errno_text("read");
            break;
        }
        if (n == 0)
            break;
        if (!write_all(out, buffer.data(), static_cast<std::size_t>(n)))
        {
            error = errno_text("write");
            break;
        }
    }
    ::close(in);
    ::fsync(out);
    ::close(out);
    if (error.empty() && ::rename(temporary.c_str(), dst.c_str()) != 0)
        error = errno_text(("rename to " + dst).c_str());
    if (!error.empty())
        ::unlink(temporary.c_str());
    return error;
}

long long file_size(const std::string &path)
{
    if (through_host(path))
    {
        bool folder = false;
        long long size = -1;
        std::string mode, answer;
        return host_stat(path, &folder, &size, &mode, &answer) && !folder ? size : -1;
    }
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
        return -1;
    return static_cast<long long>(info.st_size);
}

bool same_content(const std::string &a, const std::string &b)
{
    const long long size = file_size(a);
    if (size < 0 || size != file_size(b))
        return false;
    std::FILE *fa = std::fopen(a.c_str(), "rb");
    std::FILE *fb = std::fopen(b.c_str(), "rb");
    bool same = fa != nullptr && fb != nullptr;
    std::vector<char> ba(1 << 18), bb(1 << 18);
    while (same)
    {
        const std::size_t na = std::fread(ba.data(), 1, ba.size(), fa);
        const std::size_t nb = std::fread(bb.data(), 1, bb.size(), fb);
        if (na != nb || std::memcmp(ba.data(), bb.data(), na) != 0)
            same = false;
        if (na == 0)
            break;
    }
    if (fa != nullptr)
        std::fclose(fa);
    if (fb != nullptr)
        std::fclose(fb);
    return same;
}

bool exists(const std::string &path)
{
    if (through_host(path))
    {
        bool folder = false;
        long long size = -1;
        std::string mode, answer;
        return host_stat(path, &folder, &size, &mode, &answer);
    }
    return here(path);
}

bool make_dirs(const std::string &path)
{
    // The host makes its data folder itself.
    if (through_host(path))
    {
        bool folder = false;
        long long size = -1;
        std::string mode, answer;
        return host_stat(path, &folder, &size, &mode, &answer) && folder;
    }
    // Only the result counts: a title may get another error than EEXIST for a parent it can't change, such as /data.
    for (std::size_t slash = path.find('/', 1);; slash = path.find('/', slash + 1))
    {
        ::mkdir(path.substr(0, slash).c_str(), 0777);
        if (slash == std::string::npos)
            break;
    }
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

std::string open_error(const std::string &path)
{
    if (through_host(path))
    {
        bool folder = false;
        long long size = -1;
        std::string mode, answer;
        return host_stat(path, &folder, &size, &mode, &answer) ? std::string() : answer;
    }
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return diag::errno_name(errno);
    ::close(fd);
    return {};
}

std::string describe_path(const std::string &path)
{
    if (through_host(path))
    {
        bool folder = false;
        long long size = -1;
        std::string mode, answer;
        if (!host_stat(path, &folder, &size, &mode, &answer))
            return "this title has no /data; the host says: " + answer;
        return std::string("through the host: ") + (folder ? "folder" : "file size=" + std::to_string(size)) + " mode=" + mode;
    }
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0)
        return errno == ENOENT ? std::string("missing") : "stat " + diag::errno_name(errno);
    char mode[16];
    std::snprintf(mode, sizeof mode, "%04o", static_cast<unsigned>(info.st_mode & 07777));
    const bool folder = S_ISDIR(info.st_mode);
    std::string text = folder                  ? std::string("folder")
                       : S_ISREG(info.st_mode) ? "file size=" + std::to_string(static_cast<long long>(info.st_size))
                                               : std::string("other");
    text += std::string(" mode=") + mode + " uid=" + std::to_string(info.st_uid) + " gid=" + std::to_string(info.st_gid);
    const auto usable = [&](int flags) {
        const int fd = ::open(path.c_str(), flags);
        if (fd < 0)
            return diag::errno_name(errno);
        ::close(fd);
        return std::string("ok");
    };
    text += " read=" + usable(O_RDONLY);
    // Opening for writing without O_TRUNC or O_CREAT leaves a file as it is. For a folder only trying tells:
    // access() answers EPERM in a title even where it may write.
    if (!folder)
    {
        text += " write=" + usable(O_WRONLY);
        return text;
    }
    const std::string probe = path + "/.prospero_write_test";
    const int fd = ::open(probe.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    text += " write=" + (fd >= 0 ? std::string("ok") : diag::errno_name(errno));
    if (fd >= 0)
    {
        ::close(fd);
        ::unlink(probe.c_str());
    }
    return text;
}

std::uint32_t firmware_version()
{
    std::uint32_t version = 0;
    std::size_t size = sizeof version;
    return ::sysctlbyname("kern.sdk_version", &version, &size, nullptr, 0) == 0 ? version : 0;
}

Exchange exchange(std::uint16_t port, const std::string &request, bool (*last_line)(const std::string &),
                  int timeout_ms)
{
    Exchange result;
    const int fd = connect_loopback(port, 1500);
    if (fd < 0)
    {
        result.connect_refused = errno == ECONNREFUSED;
        result.error = result.connect_refused ? "not running" : errno_text("connect");
        return result;
    }
    if (!write_all(fd, request.data(), request.size()))
    {
        result.error = errno_text("send");
        ::close(fd);
        return result;
    }
    const long long deadline = now_ms() + timeout_ms;
    std::string buffer;
    bool done = false;
    while (!done)
    {
        const long long left = deadline - now_ms();
        if (left <= 0)
        {
            result.error = "no answer in time";
            break;
        }
        pollfd p{fd, POLLIN, 0};
        const int ready = ::poll(&p, 1, static_cast<int>(left > 250 ? 250 : left));
        if (ready < 0 && errno != EINTR)
        {
            result.error = errno_text("poll");
            break;
        }
        if (ready <= 0)
            continue;
        char chunk[16384];
        const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
        if (n <= 0)
        {
            if (result.lines.empty())
                result.error = "connection closed";
            break;
        }
        buffer.append(chunk, static_cast<std::size_t>(n));
        for (std::size_t nl; !done && (nl = buffer.find('\n')) != std::string::npos;)
        {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            result.lines.push_back(line);
            done = last_line(line);
        }
    }
    ::close(fd);
    return result;
}

bool port_open(std::uint16_t port, int timeout_ms)
{
    const int fd = connect_loopback(port, timeout_ms);
    if (fd < 0)
        return false;
    ::close(fd);
    return true;
}

std::string local_ipv4()
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return {};
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(9);
    target.sin_addr.s_addr = htonl(0x0A000001); // 10.0.0.1; a route is all that is needed
    sockaddr_in local{};
    socklen_t length = sizeof local;
    std::string text;
    if (::connect(fd, reinterpret_cast<sockaddr *>(&target), sizeof target) == 0 &&
        ::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &length) == 0 && local.sin_addr.s_addr != 0)
    {
        const std::uint32_t a = ntohl(local.sin_addr.s_addr);
        text = std::to_string(a >> 24) + "." + std::to_string((a >> 16) & 255) + "." + std::to_string((a >> 8) & 255) +
               "." + std::to_string(a & 255);
    }
    ::close(fd);
    return text;
}

std::string send_file(std::uint16_t port, const std::string &path)
{
    const int in = ::open(path.c_str(), O_RDONLY);
    if (in < 0)
        return errno_text(("open " + path).c_str());
    const int fd = connect_loopback(port, 3000);
    if (fd < 0)
    {
        const std::string error = errno == ECONNREFUSED ? std::string("no ELF loader on port ") + std::to_string(port)
                                                        : errno_text("connect");
        ::close(in);
        return error;
    }
    std::vector<char> buffer(1 << 18);
    std::string error;
    long long total = 0;
    for (;;)
    {
        const ssize_t n = ::read(in, buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
        {
            error = errno_text("read");
            break;
        }
        if (n == 0)
            break;
        const char *p = buffer.data();
        std::size_t left = static_cast<std::size_t>(n);
        while (left > 0)
        {
            const ssize_t sent = ::send(fd, p, left, 0);
            if (sent < 0 && errno == EINTR)
                continue;
            if (sent <= 0)
            {
                error = errno_text("send");
                break;
            }
            p += sent;
            left -= static_cast<std::size_t>(sent);
            total += sent;
        }
        if (!error.empty())
            break;
    }
    ::close(in);
    ::close(fd);
    if (error.empty() && total == 0)
        error = "the file is empty";
    return error;
}

} // namespace psapp::io
