// SPDX-License-Identifier: GPL-3.0-only
// Sunshine's platf:: layer for the PS5: networking, game audio from Avcap2, the native virtual pad, keyboard
// and mouse, and placeholders for what is not forwarded (touch screen, pen).
#include "audio/channel_map.hpp"
#include "input/gamepad_map.hpp"
#include "input/keyboard_map.hpp"
#include "input/pad_roles.hpp"
#include "input/pad_touch.hpp"
#include "platform/ps5/kbm.h"
#include "platform/ps5/pad.h"
#include "platform/ps5/haptic_envelope.hpp"
#include "platform/ps5/haptic_guard.h"
#include "src/stream.h"
#include "platform/ps5/shared_capture.hpp"
#include "src/config.h"
#include "src/file_handler.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <pthread_np.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <thread>
#include <boost/asio/ip/address.hpp>

using namespace std::literals;

extern "C" int sceNetGetMacAddress(uint8_t address[6], int flags);  // libSceNet, linked by default

namespace platf {

std::unique_ptr<deinit_t> init() { return std::make_unique<deinit_t>(); }

namespace publish {
// Upstream calls this from main.cpp, which the PS5 host replaces; mDNS runs from main_ps5.cpp
// (src/platform/ps5/mdns_responder.cpp, ROADMAP P9.2).
std::unique_ptr<deinit_t> start() { return nullptr; }
}

std::filesystem::path appdata() { return "/data/prosperosunshine"; }

std::string get_host_name() {
    char name[256] = {};
    if (gethostname(name, sizeof name - 1) == 0 && name[0]) return name;
    return "PS5";
}

std::string get_mac_address(const std::string_view &address) {
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0) return "00:00:00:00:00:00";
    std::string interface_name, mac = "00:00:00:00:00:00";
    for (auto *a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || (a->ifa_addr->sa_family != AF_INET && a->ifa_addr->sa_family != AF_INET6)) continue;
        if (from_sockaddr(a->ifa_addr) == address) interface_name = a->ifa_name;
    }
    for (auto *a = list; a && !interface_name.empty(); a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_LINK || interface_name != a->ifa_name) continue;
        const auto *dl = reinterpret_cast<const sockaddr_dl *>(a->ifa_addr);
        if (dl->sdl_alen != 6) continue;
        const auto *b = reinterpret_cast<const unsigned char *>(LLADDR(dl));
        char buf[18];
        std::snprintf(buf, sizeof buf, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
        mac = buf;
    }
    freeifaddrs(list);
    // Without a match above, ask the network library.
    if (mac == "00:00:00:00:00:00") {
        uint8_t b[6] = {};
        if (sceNetGetMacAddress(b, 0) == 0) {
            char buf[18];
            std::snprintf(buf, sizeof buf, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
            mac = buf;
        }
    }
    return mac;
}

std::string from_sockaddr(const sockaddr *const addr) { return from_sockaddr_ex(addr).second; }

std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const addr) {
    char text[INET6_ADDRSTRLEN] = {};
    std::uint16_t port = 0;
    if (addr->sa_family == AF_INET6) {
        const auto *in6 = reinterpret_cast<const sockaddr_in6 *>(addr);
        inet_ntop(AF_INET6, &in6->sin6_addr, text, sizeof text);
        port = ntohs(in6->sin6_port);
    } else if (addr->sa_family == AF_INET) {
        const auto *in = reinterpret_cast<const sockaddr_in *>(addr);
        inet_ntop(AF_INET, &in->sin_addr, text, sizeof text);
        port = ntohs(in->sin_port);
    }
    return {port, text};
}

void set_thread_name(std::string_view name) {
    const std::string copy(name.substr(0, 15));
    pthread_set_name_np(pthread_self(), copy.c_str());
}

void adjust_thread_priority(thread_priority_e) {}
void streaming_will_start() {}
void streaming_will_stop() {}
void open_url(const std::string &) {}

std::unique_ptr<deinit_t> enable_socket_qos(uintptr_t, boost::asio::ip::address &, uint16_t, qos_data_type_e, bool) {
    return nullptr;
}

namespace {
socklen_t to_sockaddr(const boost::asio::ip::address &address, uint16_t port, sockaddr_storage &out) {
    std::memset(&out, 0, sizeof out);
    if (address.is_v6()) {
        auto *in6 = reinterpret_cast<sockaddr_in6 *>(&out);
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(port);
        const auto bytes = address.to_v6().to_bytes();
        std::memcpy(&in6->sin6_addr, bytes.data(), bytes.size());
        return sizeof(sockaddr_in6);
    }
    auto *in = reinterpret_cast<sockaddr_in *>(&out);
    in->sin_family = AF_INET;
    in->sin_port = htons(port);
    const auto bytes = address.to_v4().to_bytes();
    std::memcpy(&in->sin_addr, bytes.data(), bytes.size());
    return sizeof(sockaddr_in);
}

bool send_one(int fd, const sockaddr_storage &to, socklen_t to_len, const char *header, size_t header_size,
              const char *payload, size_t payload_size) {
    iovec iov[2];
    int count = 0;
    if (header_size) iov[count++] = {const_cast<char *>(header), header_size};
    iov[count++] = {const_cast<char *>(payload), payload_size};
    msghdr msg{};
    msg.msg_name = const_cast<sockaddr_storage *>(&to);
    msg.msg_namelen = to_len;
    msg.msg_iov = iov;
    msg.msg_iovlen = count;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (sendmsg(fd, &msg, 0) >= 0) return true;
        // A full send buffer on a nonblocking socket: give the NIC a moment instead of dropping.
        if (errno != EAGAIN && errno != ENOBUFS) return false;
        std::this_thread::sleep_for(100us);
    }
    return false;
}
}

bool send_batch(const batched_send_info_t &info) {
    sockaddr_storage to;
    const socklen_t to_len = to_sockaddr(info.target_address, info.target_port, to);
    for (size_t i = 0; i < info.block_count; ++i) {
        const auto block = info.buffer_for_payload_offset((info.block_offset + i) * info.payload_size);
        const size_t size = std::min(info.payload_size, block.size);
        if (!send_one(static_cast<int>(info.native_socket), to, to_len, info.headers ? info.headers + (info.block_offset + i) * info.header_size : nullptr,
                      info.headers ? info.header_size : 0, block.buffer, size))
            return false;
    }
    return true;
}

bool send(send_info_t &info) {
    sockaddr_storage to;
    const socklen_t to_len = to_sockaddr(info.target_address, info.target_port, to);
    return send_one(static_cast<int>(info.native_socket), to, to_len, info.header, info.header_size, info.payload,
                    info.payload_size);
}

namespace {
class sleep_timer final : public high_precision_timer {
public:
    void sleep_for(const std::chrono::nanoseconds &duration) override { std::this_thread::sleep_for(duration); }
    explicit operator bool() override { return true; }
};
}

std::unique_ptr<high_precision_timer> create_high_precision_timer() { return std::make_unique<sleep_timer>(); }

// Silent microphone: keeps the audio stream alive with correctly paced zero frames.
namespace {
class silent_mic final : public mic_t {
public:
    silent_mic(int channels, std::uint32_t rate, std::uint32_t frame_size)
        : samples_(static_cast<size_t>(channels) * frame_size),
          period_(std::chrono::nanoseconds(1'000'000'000LL * frame_size / rate)),
          next_(std::chrono::steady_clock::now()) {}
    capture_e sample(std::vector<float> &frame_buffer) override {
        next_ += period_;
        std::this_thread::sleep_until(next_);
        frame_buffer.assign(samples_, 0.0f);
        return capture_e::ok;
    }

private:
    size_t samples_;
    std::chrono::nanoseconds period_;
    std::chrono::steady_clock::time_point next_;
};

// Game audio from Avcap2 as raw PCM: float32, stereo, 48 kHz, 1024 frames per packet (R2). Sunshine's
// own Opus encoder then cuts the frames Moonlight negotiated (5 ms by default). With the app's "Surround 5.1/7.1"
// (surround_sound) and a client that asks for 5.1 or 7.1, the channel is read as encoder type 3, 12 channels, and each
// microphone gets its own layout from it (audio/channel_map.hpp). Without the setting, or when type 3 fails, stereo.
//
// When the capture session is lost (a game start takes the resource), replaced by a reopen, or not
// there yet as the stream starts, the audio channel is gone; the microphone then delivers paced
// silence and reopens its channel on the current session every half second, so the stream itself
// never ends because of audio.
//
// The console has one audio channel. With several streams (a second player), the microphone that opened it reads it
// and hands every packet to all microphones; the others only wait for theirs. When the reading one goes, the next to
// notice opens the channel anew.
class avcap_mic;
struct audio_hub {
    std::mutex mutex;
    std::condition_variable arrived;
    std::vector<avcap_mic *> mics;
    avcap_mic *reader = nullptr;
    int source_channels = 2;  // of the channel the reader opened: 2 (type 0) or 12 (type 3)
    int wanted_channels = 2;  // the widest layout any microphone asks for
    bool surround_enabled = false;  // the app's Surround switch
    // Type 3's level per channel, logged every 30 s: which channels carry sound on a console with surround output.
    std::array<double, prospero::kAvcapSurroundChannels> squares{};
    std::size_t frames = 0;
    std::chrono::steady_clock::time_point levels_at{};
};

class avcap_mic final : public mic_t {
public:
    static std::unique_ptr<mic_t> open(int channels, std::uint32_t sample_rate, std::uint32_t frame_size, bool surround) {
        int rc = 0;
        auto shared = prospero::SharedCapture::acquire(rc);
        if (!shared) BOOST_LOG(warning) << "PS5 audio: capture session unavailable: "sv << rc << ", retrying"sv;
        auto mic = std::unique_ptr<avcap_mic>(new avcap_mic(std::move(shared), channels, sample_rate, frame_size));
        {
            std::lock_guard lock(hub_.mutex);
            hub_.surround_enabled = surround;
        }
        // A title started with the stream can hold the capture for a moment. Returning no microphone here left the
        // whole stream silent: Sunshine only asks for a new one after a reinit, which the silent fallback never sends.
        if (mic->another_reads()) BOOST_LOG(info) << "PS5 audio: sharing the channel another stream reads"sv;
        else if (!mic->shared_ || !mic->open_channel(mic->wanted_channels()))
            mic->retry_at_ = std::chrono::steady_clock::now() + 500ms;
        return mic;
    }

    ~avcap_mic() override {
        if (open_) close_channel();
        std::lock_guard lock(hub_.mutex);
        std::erase(hub_.mics, this);
        hub_.wanted_channels = wanted_channels_locked();
    }

    capture_e sample(std::vector<float> &frame_buffer) override {
        const size_t needed = static_cast<size_t>(frame_size_) * static_cast<size_t>(channels_);
        const auto start = std::chrono::steady_clock::now();
        for (;;) {
            bool follower = false;
            {
                std::unique_lock lock(hub_.mutex);
                follower = hub_.reader && hub_.reader != this;
                if (follower) hub_.arrived.wait_for(lock, 100ms, [&] { return pending_.size() >= needed; });
                if (pending_.size() >= needed) return take(frame_buffer, needed);
            }
            // A reader that sends nothing for a while is treated like a lost channel: silence, and a new try later.
            if (follower) {
                if (std::chrono::steady_clock::now() - start > 500ms) return silence(frame_buffer);
                continue;
            }
            if (open_ && generation_ != shared_->generation()) {
                set_open(false);  // session was reopened
                surround_failed_ = false;  // a new session may allow type 3 again
            }
            if (!open_) return silence(frame_buffer);
            // A stream that joined with a wider layout makes the reader open the 12-channel bed.
            if (source_channels_ == 2 && !surround_failed_ && wanted_channels() > 2 && surround_enabled()) {
                close_channel();
                if (!open_channel(wanted_channels())) return silence(frame_buffer);
            }
            const int rc = shared_->with_session(
                [&](ps_capture *s) { return ps_capture_audio_read(s, packet_.data(), packet_.size(), header_); });
            if (rc < 0) {
                BOOST_LOG(warning) << "PS5 audio: ReadAudio failed: "sv << rc << ", reopening"sv;
                if (source_channels_ == prospero::kAvcapSurroundChannels) surround_failed_ = true;
                close_channel();
                return silence(frame_buffer);
            }
            if (rc == 0) {
                // A stalled channel (no packet for a while) is treated like a lost one.
                if (std::chrono::steady_clock::now() - start > 500ms) {
                    close_channel();
                    return silence(frame_buffer);
                }
                std::this_thread::sleep_for(1ms);
                continue;
            }
            const size_t frame_bytes = static_cast<size_t>(source_channels_) * sizeof(float);
            if (static_cast<size_t>(rc) % frame_bytes != 0) {
                BOOST_LOG(warning) << "PS5 audio: packet of "sv << rc << " bytes is not "sv << source_channels_
                                   << " channels, dropped"sv;
                continue;
            }
            hand_out(reinterpret_cast<const float *>(packet_.data()), static_cast<size_t>(rc) / sizeof(float));
        }
    }

private:
    avcap_mic(std::shared_ptr<prospero::SharedCapture> shared, int channels, std::uint32_t sample_rate,
              std::uint32_t frame_size)
        : shared_(std::move(shared)), channels_(channels), frame_size_(frame_size),
          period_(std::chrono::nanoseconds(1'000'000'000LL * frame_size / sample_rate)), packet_(0xC000) {
        std::lock_guard lock(hub_.mutex);
        hub_.mics.push_back(this);
        hub_.wanted_channels = std::max(hub_.wanted_channels, channels_);
    }

    bool another_reads() {
        std::lock_guard lock(hub_.mutex);
        return hub_.reader && hub_.reader != this;
    }

    int wanted_channels() {
        std::lock_guard lock(hub_.mutex);
        return hub_.wanted_channels;
    }

    // Caller holds hub_.mutex.
    static int wanted_channels_locked() {
        int wanted = 2;
        for (avcap_mic *mic : hub_.mics) wanted = std::max(wanted, mic->channels_);
        return wanted;
    }

    bool surround_enabled() {
        std::lock_guard lock(hub_.mutex);
        return hub_.surround_enabled;
    }

    void set_open(bool open) {
        open_ = open;
        std::lock_guard lock(hub_.mutex);
        if (open) {
            hub_.reader = this;
            pending_.clear();
        } else if (hub_.reader == this) {
            hub_.reader = nullptr;
            hub_.arrived.notify_all();
        }
    }

    // Every microphone gets the packet, in its own layout. One that falls behind keeps the last 200 ms, so its stream
    // doesn't lag.
    void hand_out(const float *samples, size_t count) {
        std::lock_guard lock(hub_.mutex);
        const int source = hub_.source_channels;
        const size_t frames = count / static_cast<size_t>(source);
        if (source == prospero::kAvcapSurroundChannels) log_levels(samples, frames);
        for (avcap_mic *mic : hub_.mics) {
            const size_t most = 48000 / 5 * static_cast<size_t>(mic->channels_);
            prospero::map_channels(samples, source, frames, mic->channels_, mic->pending_);
            if (mic->pending_.size() > most)
                mic->pending_.erase(mic->pending_.begin(),
                                    mic->pending_.begin() + static_cast<std::ptrdiff_t>(mic->pending_.size() - most));
        }
        hub_.arrived.notify_all();
    }

    // Caller holds hub_.mutex. pending_ is in this microphone's layout already.
    capture_e take(std::vector<float> &frame_buffer, size_t needed) {
        next_ = std::chrono::steady_clock::now();
        frame_buffer.assign(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(needed));
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(needed));
        return capture_e::ok;
    }

    // Caller holds hub_.mutex.
    static void log_levels(const float *samples, size_t frames) {
        constexpr int n = prospero::kAvcapSurroundChannels;
        for (size_t i = 0; i < frames; ++i)
            for (int c = 0; c < n; ++c) hub_.squares[c] += static_cast<double>(samples[i * n + c]) * samples[i * n + c];
        hub_.frames += frames;
        const auto now = std::chrono::steady_clock::now();
        if (now < hub_.levels_at) return;
        if (hub_.levels_at != std::chrono::steady_clock::time_point{} && hub_.frames) {
            std::string line;
            for (int c = 0; c < n; ++c) {
                char level[16];
                std::snprintf(level, sizeof level, " %.4f", std::sqrt(hub_.squares[c] / static_cast<double>(hub_.frames)));
                line += level;
            }
            BOOST_LOG(info) << "PS5 audio: 12-channel levels (RMS, channel 0 to 11):"sv << line;
        }
        hub_.squares = {};
        hub_.frames = 0;
        hub_.levels_at = now + 30s;
    }

    bool open_channel(int wanted) {
        // The generation is read under the lock that opens the channel. Read before it, a reopen in between (a game
        // starting) left the channel on the new session marked with the old generation: the next sample() then dropped
        // it without closing it, and every later open failed on the channel already there.
        unsigned generation = 0;
        int source = 2, surround_rc = 0;
        const bool try_surround = surround_enabled() && wanted > 2 && !surround_failed_;
        const int rc = shared_->exclusive([&](ps_capture *s) {
            generation = shared_->generation();
            if (try_surround) {
                surround_rc = ps_capture_audio_open(s, 3, 0, 0);
                if (!surround_rc) {
                    source = prospero::kAvcapSurroundChannels;
                    return 0;
                }
            }
            return ps_capture_audio_open(s, 0, 0, 0);
        });
        if (rc) {
            BOOST_LOG(warning) << "PS5 audio: OpenAudio failed: "sv << rc;
            return false;
        }
        if (try_surround && surround_rc) {
            surround_failed_ = true;
            BOOST_LOG(warning) << "PS5 audio: 12 channels refused ("sv << surround_rc << "), stereo"sv;
        } else if (try_surround) {
            BOOST_LOG(info) << "PS5 audio: 12 channels from the console for "sv << channels_ << " speakers"sv;
        }
        source_channels_ = source;
        {
            std::lock_guard lock(hub_.mutex);
            hub_.source_channels = source;
            hub_.levels_at = {};
        }
        generation_ = generation;
        set_open(true);
        return true;
    }

    void close_channel() {
        // After a reopen the channel died with the old session; closing would hit the new one. Checked under the lock,
        // so a reopen can't slip in between.
        shared_->exclusive([&](ps_capture *s) { return generation_ == shared_->generation() ? ps_capture_audio_close(s) : 0; });
        set_open(false);
    }

    capture_e silence(std::vector<float> &frame_buffer) {
        if (std::chrono::steady_clock::now() >= retry_at_ && !another_reads()) {
            // Only the session video opened: if video failed too, the stream is ending, and opening one here would hold
            // up its teardown.
            if (!shared_) shared_ = prospero::SharedCapture::attach();
            if (shared_ && open_channel(wanted_channels())) BOOST_LOG(info) << "PS5 audio: channel reopened"sv;
            retry_at_ = std::chrono::steady_clock::now() + 500ms;
        }
        {
            // What arrives during the silence would only play late.
            std::lock_guard lock(hub_.mutex);
            pending_.clear();
        }
        next_ = std::max(next_ + period_, std::chrono::steady_clock::now() - period_);
        std::this_thread::sleep_until(next_);
        frame_buffer.assign(static_cast<size_t>(frame_size_) * channels_, 0.0f);
        return capture_e::ok;
    }

    static inline audio_hub hub_;
    std::shared_ptr<prospero::SharedCapture> shared_;
    int channels_;
    int source_channels_ = 2;  // of the channel this microphone opened: 2 (type 0) or 12 (type 3)
    bool surround_failed_ = false;  // type 3 was refused or failed; don't retry until a new session
    std::uint32_t frame_size_;
    std::chrono::nanoseconds period_;
    std::vector<uint8_t> packet_;
    uint8_t header_[0x10] = {};
    std::vector<float> pending_;  // guarded by hub_.mutex
    bool open_ = false;
    unsigned generation_ = 0;
    std::chrono::steady_clock::time_point next_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point retry_at_{};
};

class silent_audio_control final : public audio_control_t {
public:
    int set_sink(const std::string &) override { return 0; }
    std::unique_ptr<mic_t> microphone(const std::uint8_t *, int channels, std::uint32_t sample_rate,
                                      std::uint32_t frame_size, bool, bool) override {
        if (sample_rate == 48000) {
            // The app's "Surround 5.1/7.1" (off by default). Sunshine doesn't know the key and leaves it alone.
            const auto settings = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));
            const auto setting = settings.find("surround_sound");
            const bool surround = setting != settings.end() && setting->second == "1";
            if (auto mic = avcap_mic::open(channels, sample_rate, frame_size, surround)) {
                BOOST_LOG(info) << "PS5 audio: game audio via Avcap2, "sv << channels << " channels, "sv << frame_size
                                << " frames per packet"sv;
                return mic;
            }
        }
        BOOST_LOG(warning) << "PS5 audio: sending silence (rate "sv << sample_rate << ')';
        return std::make_unique<silent_mic>(channels, sample_rate, frame_size);
    }
    bool is_sink_available(const std::string &) override { return true; }
    std::optional<sink_t> sink_info() override {
        sink_t sink;
        sink.host = "ps5";
        return sink;
    }
};
}

std::unique_ptr<audio_control_t> audio_control() { return std::make_unique<silent_audio_control>(); }

// Input: one native virtual DualSense per Moonlight gamepad slot, and one keyboard and one mouse shared by all clients.
struct input_raw_t {
    std::mutex mutex;
    std::array<ps_pad *, MAX_GAMEPADS> pads{};
    // A guest's slot whose pad isn't open yet: it opens on the controller's first use (wakes_pad, GitHub issue 26).
    std::array<bool, MAX_GAMEPADS> waiting{};
    std::array<bool, MAX_GAMEPADS> active{};  // last state had something pressed or deflected
    std::array<bool, MAX_GAMEPADS> back{};    // Select is down
    std::array<bool, MAX_GAMEPADS> ps_held{}; // a guest's PS or Create press that was held back, logged once per press
    std::array<std::chrono::steady_clock::time_point, MAX_GAMEPADS> back_since{};
    std::array<feedback_queue_t, MAX_GAMEPADS> feedback{};
    std::array<uint8_t, MAX_GAMEPADS> client_index{};
    std::array<std::array<uint8_t, 2>, MAX_GAMEPADS> motors{};
    std::array<std::array<uint8_t, 3>, MAX_GAMEPADS> colors{};
    std::array<prospero::TriggerBlock, MAX_GAMEPADS> triggers{};
    std::array<prospero::PadTouch, MAX_GAMEPADS> touch{};
    // Added on a client's first key or mouse event, removed when no session is left (client_session).
    ps_keyboard *keyboard = nullptr;
    ps_mouse *mouse = nullptr;
    bool keyboard_failed = false, mouse_failed = false;
    prospero::KeyboardState keys;
    uint8_t mouse_buttons = 0;
    prospero::WheelSteps wheel, tilt;
    std::chrono::steady_clock::time_point key_down_at, button_down_at;
    std::optional<std::pair<float, float>> pointer;  // the last absolute position, for abs_mouse
    int sessions = 0;
    uint32_t user = 0;
    bool ps_all_players = false;  // ps_button_all_players: every pad may press PS and Create, not only the host's
    prospero::PadRoles<MAX_GAMEPADS> roles;  // who is the host: a paired device, not a slot (GitHub issue 15)
    std::jthread output_thread;
    std::jthread haptic_thread;
    prospero::HapticEnvelope haptics;
    std::atomic_bool haptics_enabled{false};  // haptic_mode is rumble or hd
    std::atomic_bool haptics_hd{false};       // haptic_mode is hd
    std::atomic<float> haptics_gain{1.0f};    // haptic_strength
    std::atomic_int knock_percent{70};        // trigger_knock_strength
};

static void set_motors(input_raw_t &input, int nr, uint8_t large, uint8_t small) {
    if (input.motors[nr] == std::array<uint8_t, 2>{large, small}) return;
    input.motors[nr] = {large, small};
    BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " motors "sv << int(large) << ',' << int(small);
    if (input.feedback[nr]) input.feedback[nr]->raise(gamepad_feedback_msg_t::make_rumble(input.client_index[nr], large * 257, small * 257));
}

static void set_color(input_raw_t &input, int nr, uint8_t r, uint8_t g, uint8_t b) {
    if (input.colors[nr] == std::array<uint8_t, 3>{r, g, b}) return;
    input.colors[nr] = {r, g, b};
    BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " light bar "sv << int(r) << ',' << int(g) << ',' << int(b);
    if (input.feedback[nr]) input.feedback[nr]->raise(gamepad_feedback_msg_t::make_rgb_led(input.client_index[nr], r, g, b));
}

static void set_triggers(input_raw_t &input, int nr, const prospero::TriggerBlock &block) {
    if (input.triggers[nr] == block) return;
    const auto effects = prospero::map_trigger_effects(block, input.triggers[nr], input.knock_percent);
    input.triggers[nr] = block;
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (const uint8_t byte : block) {
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " triggers "sv << hex;
    if (effects.flags && input.feedback[nr]) {
        input.feedback[nr]->raise(gamepad_feedback_msg_t::make_adaptive_triggers(input.client_index[nr], effects.flags, effects.type_left, effects.type_right, effects.left, effects.right));
    }
}

// Reads what the system wants from each pad at the cadence of Remote Play's own reader and passes on
// changes of the motors, the light bar and the trigger effects. Only titles using the classic two-motor rumble set the motors;
// see docs/re/pad-output.md.
static void poll_pad_output(std::stop_token stop, input_raw_t *input) {
    std::array<bool, MAX_GAMEPADS> failed{};
    while (!stop.stop_requested()) {
        const auto rumble = input->haptics.tick();
        {
            std::lock_guard lock(input->mutex);
            for (int nr = 0; nr < MAX_GAMEPADS; ++nr) {
                if (!input->pads[nr]) {
                    failed[nr] = false;
                    continue;
                }
                ps_pad_output output{};
                if (const int rc = ps_pad_read_output(input->pads[nr], &output); rc) {
                    if (!failed[nr]) BOOST_LOG(warning) << "PS5 virtual pad "sv << nr << " output read failed: "sv << rc;
                    failed[nr] = true;
                    continue;
                }
                set_motors(*input, nr, std::max(output.motors[0], rumble[0]), std::max(output.motors[1], rumble[1]));
                set_color(*input, nr, output.color[0], output.color[1], output.color[2]);
                prospero::TriggerBlock triggers;
                std::memcpy(triggers.data(), output.triggers, sizeof output.triggers);
                set_triggers(*input, nr, triggers);
            }
        }
        std::this_thread::sleep_for(10ms);
    }
}

// While this file exists the channel stays closed, whatever the app says: the host died or faulted reading it. Delete it to try again.
static constexpr auto haptic_blocked = "/data/prosperosunshine/haptic_blocked";
static constexpr auto haptic_dump = "/data/prosperosunshine/haptic_dump.bin";
static constexpr auto haptic_code = "/data/prosperosunshine/haptic_code.bin";
// Exists from before the channel opens until it has run for a while. Found at the start, it means the host died with the channel
// open: then the channel gets blocked, so that one bad try can't break every stream that follows.
static constexpr auto haptic_attempt = "/data/prosperosunshine/haptic_attempt";

namespace {
struct haptic_read_call {
    ps_capture *session;
    void *destination;
    std::size_t capacity;
    uint8_t *header;
};
int haptic_read_step(void *argument) {
    auto *call = static_cast<haptic_read_call *>(argument);
    return ps_capture_haptic_read(call->session, call->destination, call->capacity, call->header);
}
}

// Config words to try, the likeliest first: audio's first block (size, kind, purpose, encoder, resource id) is 0x18 bytes too.
static std::vector<std::array<uint32_t, 4>> haptic_configs() {
    constexpr uint32_t id = PS_CAPTURE_HAPTIC_RESOURCE;
    std::vector<std::array<uint32_t, 4>> configs;
    for (const uint32_t purpose : {2u, 0u, 1u, 3u})
        for (const uint32_t encoder : {0u, 1u, 2u, 3u}) configs.push_back({0, purpose, encoder, id});
    configs.push_back({id, 0, 0, 0});
    configs.push_back({0, id, 0, 0});
    configs.push_back({0, 0, id, 0});
    configs.push_back({2, 0, 0, id});
    return configs;
}

// Reads the haptic channel during a stream and hands its level to poll_pad_output as rumble. Games made for the PS5 send their
// vibration only this way; Moonlight has no channel for it, but it has the two motors.
static void load_vibration_settings(input_raw_t &input) {
    // Sunshine doesn't know these keys. Read again whenever the file changes, so the app's Save takes effect within seconds, in a
    // running stream too.
    const auto settings = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));
    const auto percent = [&](const char *key, int fallback, int largest) {
        const auto found = settings.find(key);
        return found == settings.end() ? fallback : std::clamp(std::atoi(found->second.c_str()), 0, largest);
    };
    const auto mode = settings.find("haptic_mode");
    const std::string chosen = mode == settings.end() ? "off" : mode->second;
    input.haptics_enabled = chosen == "rumble" || chosen == "hd";
    input.haptics_hd = chosen == "hd";
    const int strength = percent("haptic_strength", 100, 200);
    input.haptics.set_strength(strength);
    input.haptics_gain = static_cast<float>(strength) / 100.0f;
    input.knock_percent = percent("trigger_knock_strength", 70, 100);
    const int force = percent("trigger_force", 60, 100);
    stream::dualsense_set_power_reduction(prospero::trigger_power_steps(force));
    BOOST_LOG(info) << "PS5 input: vibration "sv << chosen << " at "sv << strength << "%, trigger knock "sv << input.knock_percent.load() << "%, trigger force "sv << force << '%';
}

static void poll_haptics(std::stop_token stop, input_raw_t *input) {
    std::shared_ptr<prospero::SharedCapture> shared;
    unsigned generation = 0;
    bool open = false, gave_up = false;
    std::optional<std::pair<std::array<uint32_t, 4>, uint8_t>> working;
    std::vector<uint8_t> packet(0x10000);
    alignas(16) uint8_t header[0x400];  // only 0x18 are known; room in case the call writes more
    FILE *dump = nullptr;
    prospero::HapticSplitter splitter;
    std::array<uint8_t, prospero::kHapticBlockBytes> hd_block{};
    bool sending_hd = false;
    bool code_saved = false, faulted = false, attempt_open = false;
    auto opened_at = std::chrono::steady_clock::now();
    auto settings_checked = opened_at;
    std::filesystem::file_time_type settings_written{};
    std::error_code attempt_ec;
    if (std::filesystem::exists(haptic_attempt, attempt_ec)) {
        BOOST_LOG(warning) << "PS5 haptics: the host stopped during the last try, the channel stays closed"sv;
        std::ofstream(haptic_blocked).flush();
        std::filesystem::remove(haptic_attempt, attempt_ec);
    }
    const auto attempt_over = [&] {
        if (!attempt_open) return;
        std::error_code ec;
        std::filesystem::remove(haptic_attempt, ec);
        attempt_open = false;
    };
    long dumped = 0, logged = 0, packets = 0, bytes = 0;
    int smallest = 0, largest = 0;
    float peak_low = 0, peak_high = 0;
    auto reported = std::chrono::steady_clock::now();

    const auto try_open = [&](ps_capture *s) {
        if (generation != shared->generation()) return false;
        if (working) return ps_capture_haptic_open(s, working->first.data(), working->second) == 0;
        for (const uint8_t flag : {uint8_t(0), uint8_t(1)}) {
            for (const auto &words : haptic_configs()) {
                const int rc = ps_capture_haptic_open(s, words.data(), flag);
                BOOST_LOG(info) << "PS5 haptics: open "sv << util::hex(words[0]).to_string_view() << ' ' << util::hex(words[1]).to_string_view() << ' '
                                << util::hex(words[2]).to_string_view() << ' ' << util::hex(words[3]).to_string_view() << " flag "sv << int(flag) << ": "sv
                                << util::hex(rc).to_string_view();
                if (rc == 0) {
                    working = std::make_pair(words, flag);
                    return true;
                }
            }
        }
        return false;
    };
    const auto release = [&] {
        if (open && shared) shared->exclusive([&](ps_capture *s) { return generation == shared->generation() ? ps_capture_haptic_close(s) : 0; });
        open = false;
        shared.reset();
    };

    while (!stop.stop_requested()) {
        bool pad = false;
        {
            std::lock_guard lock(input->mutex);
            for (const auto *candidate : input->pads) pad = pad || candidate;
        }
        std::error_code ec;
        const auto checked_now = std::chrono::steady_clock::now();
        if (checked_now - settings_checked >= 2s) {
            settings_checked = checked_now;
            std::error_code time_ec;
            const auto written = std::filesystem::last_write_time(config::sunshine.config_file, time_ec);
            if (!time_ec && written != settings_written) {
                settings_written = written;
                load_vibration_settings(*input);
            }
        }
        if (!pad || !input->haptics_enabled || (!open && std::filesystem::exists(haptic_blocked, ec))) {
            release();
            attempt_over();
            gave_up = faulted;
            std::this_thread::sleep_for(500ms);
            continue;
        }
        if (!open) {
            if (gave_up) {
                std::this_thread::sleep_for(500ms);
                continue;
            }
            shared = prospero::SharedCapture::attach();
            if (!shared) {
                std::this_thread::sleep_for(200ms);
                continue;
            }
            generation = shared->generation();
            std::ofstream(haptic_attempt).flush();
            attempt_open = true;
            open = shared->exclusive(try_open);
            if (!open) {
                BOOST_LOG(warning) << "PS5 haptics: the channel didn't open, no more tries in this stream"sv;
                shared.reset();
                gave_up = true;
                attempt_over();
                continue;
            }
            BOOST_LOG(info) << "PS5 haptics: channel open"sv;
            opened_at = std::chrono::steady_clock::now();
            if (!code_saved) {
                code_saved = true;
                const uintptr_t read = shared->with_session([](ps_capture *s) { return ps_capture_haptic_read_address(s); });
                // 0x10e0 into the library on 13.00: the dump starts near its base and reaches ReadAudio and the shared reader.
                ps_haptic_dump_memory(haptic_code, read - 0x1100, 0x4000);
                BOOST_LOG(info) << "PS5 haptics: ReadHaptic at "sv << util::hex(static_cast<uint64_t>(read)).to_string_view();
            }
        }
        if (generation != shared->generation()) {
            open = false;
            shared.reset();
            continue;
        }
        ps_haptic_fault fault{};
        const int rc = shared->with_session([&](ps_capture *s) {
            haptic_read_call call{s, packet.data(), packet.size(), header};
            return ps_haptic_guarded(haptic_read_step, &call, &fault);
        });
        if (rc == PS_HAPTIC_FAULT) {
            BOOST_LOG(warning) << "PS5 haptics: read faulted, signal "sv << fault.signal << " at "sv << util::hex(fault.instruction).to_string_view()
                               << ", address "sv << util::hex(fault.address).to_string_view() << "; off until the host restarts"sv;
            shared->exclusive([](ps_capture *s) {
                ps_capture_haptic_abandon(s);
                return 0;
            });
            std::ofstream(haptic_blocked).flush();
            open = false;
            shared.reset();
            faulted = gave_up = true;
            attempt_over();
            continue;
        }
        if (attempt_open && std::chrono::steady_clock::now() - opened_at >= 5s) attempt_over();
        if (rc < 0) {
            BOOST_LOG(warning) << "PS5 haptics: read failed: "sv << rc;
            release();
            std::this_thread::sleep_for(1s);
            continue;
        }
        if (rc > 0) {
            const auto *samples = reinterpret_cast<const float *>(packet.data());
            const auto frames = std::min(prospero::kHapticFrames, static_cast<std::size_t>(rc) / (2 * sizeof(float)));
            const auto levels = splitter.process(samples, frames);
            // The waveform itself where the client takes it, else as much of it as two motor values can hold.
            const bool hd = input->haptics_hd && stream::dualsense_haptics_wanted();
            if (hd != sending_hd) {
                sending_hd = hd;
                BOOST_LOG(info) << "PS5 haptics: "sv << (hd ? "waveform to the client"sv : "rumble"sv);
            }
            if (hd && frames == prospero::kHapticFrames) {
                prospero::haptic_block(samples, input->haptics_gain.load(), hd_block.data());
                stream::dualsense_haptics_push(hd_block.data());
            } else {
                input->haptics.feed(levels);
            }
            ++packets;
            bytes += rc;
            smallest = smallest ? std::min(smallest, rc) : rc;
            largest = std::max(largest, rc);
            peak_low = std::max(peak_low, levels.low);
            peak_high = std::max(peak_high, levels.high);
            if (logged < 4 && levels.high > 0.01f) {
                ++logged;
                static constexpr char digits[] = "0123456789abcdef";
                std::string hex;
                for (int i = 0; i < 0x18 + 32 && i < 0x18 + rc; ++i) {
                    const uint8_t byte = i < 0x18 ? header[i] : packet[i - 0x18];
                    hex += digits[byte >> 4];
                    hex += digits[byte & 15];
                    if (i == 0x17) hex += ' ';
                }
                BOOST_LOG(info) << "PS5 haptics: packet of "sv << rc << " bytes: "sv << hex;
            }
            if (dumped < (4 << 20)) {
                if (!dump) dump = std::fopen(haptic_dump, "wb");
                if (dump) {
                    std::fwrite(packet.data(), 1, static_cast<std::size_t>(rc), dump);
                    std::fflush(dump);
                    dumped += rc;
                }
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - reported >= 5s) {
            if (packets)
                BOOST_LOG(info) << "PS5 haptics: "sv << packets << " packets, "sv << bytes << " bytes, "sv << smallest << ".."sv << largest
                                << " each, peak low "sv << peak_low << " high "sv << peak_high;
            reported = now;
            packets = bytes = smallest = largest = 0;
            peak_low = peak_high = 0;
        }
        if (rc == 0) std::this_thread::sleep_for(2ms);
    }
    release();
    attempt_over();
    if (dump) std::fclose(dump);
}

static void close_keyboard_mouse(input_raw_t &input) {
    if (input.keyboard) {
        const int rc = ps_keyboard_close(input.keyboard);
        BOOST_LOG(info) << "PS5 virtual keyboard detached ("sv << rc << ')';
    }
    if (input.mouse) {
        const int rc = ps_mouse_close(input.mouse);
        BOOST_LOG(info) << "PS5 virtual mouse detached ("sv << rc << ')';
    }
    input.keyboard = nullptr;
    input.mouse = nullptr;
    input.keyboard_failed = input.mouse_failed = false;
    input.keys.clear();
    input.mouse_buttons = 0;
    input.wheel.clear();
    input.tilt.clear();
    input.pointer.reset();
}

void freeInput(input_raw_t *input) {
    if (!input) return;
    input->haptic_thread.request_stop();
    if (input->haptic_thread.joinable()) input->haptic_thread.join();
    input->output_thread.request_stop();
    if (input->output_thread.joinable()) input->output_thread.join();
    for (auto *&pad : input->pads) {
        ps_pad_close(pad);
        pad = nullptr;
    }
    close_keyboard_mouse(*input);
    delete input;
}

input_t input() {
    auto *raw = new input_raw_t;
    raw->user = ps_pad_default_user();
    // Not the user's id: this log goes into the report people post (app/src/psapp/diag.cpp).
    BOOST_LOG(info) << (raw->user ? "PS5 input: virtual pads ready"sv : "PS5 input: no user for virtual pads"sv);
    // The app's "PS for all players" (off by default). Sunshine doesn't know the key and leaves it alone.
    const auto settings = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));
    const auto all = settings.find("ps_button_all_players");
    raw->ps_all_players = all != settings.end() && all->second == "1";
    BOOST_LOG(info) << (raw->ps_all_players ? "PS5 input: every player can press PS and Create"sv
                                            : "PS5 input: only the host can press PS and Create"sv);
    load_vibration_settings(*raw);
    raw->output_thread = std::jthread(poll_pad_output, raw);
    raw->haptic_thread = std::jthread(poll_haptics, raw);
    return input_t{raw};
}

// A client only sends the sensors it is asked for, and a new client has to be asked anew. One without sensors ignores this.
static void request_motion(const input_raw_t &input, int nr) {
    if (!input.feedback[nr]) return;
    for (const uint8_t type : {prospero::moonlight::motion_accel, prospero::moonlight::motion_gyro}) {
        input.feedback[nr]->raise(gamepad_feedback_msg_t::make_motion_event_state(input.client_index[nr], type, 100));
    }
}

namespace {
// Which paired device each session's feedback queue belongs to: stream::session::start() tells (patch 0016), and
// alloc_gamepad, which only gets the queue, asks. The weak pointer lets an ended session's entry go.
struct gamepad_owner_t {
    std::weak_ptr<void> queue;
    const void *key;
    std::string owner;
};
std::mutex gamepad_owners_mutex;
std::vector<gamepad_owner_t> gamepad_owners;

std::string gamepad_owner(const feedback_queue_t &queue) {
    std::lock_guard lock(gamepad_owners_mutex);
    for (const auto &entry : gamepad_owners)
        if (entry.key == queue.get() && !entry.queue.expired()) return entry.owner;
    return {};
}
}  // namespace

void set_gamepad_owner(const feedback_queue_t &queue, std::string_view owner) {
    if (!queue) return;
    std::lock_guard lock(gamepad_owners_mutex);
    std::erase_if(gamepad_owners, [&](const gamepad_owner_t &entry) { return entry.queue.expired() || entry.key == queue.get(); });
    gamepad_owners.push_back({queue, queue.get(), std::string(owner)});
}

// Caller holds input.mutex.
static bool open_pad(input_raw_t &input, int nr, bool host) {
    auto *&pad = input.pads[nr];
    const int rc = ps_pad_open(host ? input.user : PS_PAD_NO_USER, &pad);
    if (rc) {
        BOOST_LOG(error) << "PS5 virtual pad "sv << nr << " failed: "sv << rc;
        pad = nullptr;
        return false;
    }
    BOOST_LOG(info) << "PS5 virtual pad "sv << nr << " attached"sv
                    << (host ? " as the host's controller"sv
                             : input.roles.host_away() ? ", the console asks who uses it; the host is away and keeps the role"sv
                                                       : ", the console asks who uses it"sv);
    return true;
}

int alloc_gamepad(input_t &input, const gamepad_id_t &id, const gamepad_arrival_t &, feedback_queue_t feedback) {
    if (id.globalIndex < 0 || id.globalIndex >= MAX_GAMEPADS) return -1;
    const std::string owner = gamepad_owner(feedback);
    std::lock_guard lock(input->mutex);
    input->feedback[id.globalIndex] = std::move(feedback);
    input->client_index[id.globalIndex] = id.clientRelativeIndex;
    auto *&pad = input->pads[id.globalIndex];
    if (!pad && !input->waiting[id.globalIndex]) {
        // The host's pad plays as the console's user. Every other one, from another device or a second controller on the
        // host's, is a guest: the system asks who uses it, and they pick a user or a guest (pad_roles.hpp). The role is
        // the device's from the start; a guest's pad only opens on its first use, as the question can only be answered
        // from that controller and a client may announce one that never sends anything (gamepad_map.hpp, wakes_pad).
        const bool host = input->roles.attach(id.globalIndex, owner);
        // No device ids in the log: it goes into the report people post.
        if (!host) {
            input->waiting[id.globalIndex] = true;
            BOOST_LOG(info) << "PS5 virtual pad "sv << id.globalIndex << " announced, opens when it is used"sv
                            << (owner.empty() ? " (device unknown)"sv : ""sv);
        } else if (!open_pad(*input, id.globalIndex, true)) {
            input->roles.detach(id.globalIndex);
            return -1;
        }
    }
    request_motion(*input, id.globalIndex);
    return 0;
}

int rebind_gamepad(input_t &input, const gamepad_id_t &id, feedback_queue_t feedback) {
    if (id.globalIndex < 0 || id.globalIndex >= MAX_GAMEPADS) return -1;
    std::lock_guard lock(input->mutex);
    input->feedback[id.globalIndex] = std::move(feedback);
    input->client_index[id.globalIndex] = id.clientRelativeIndex;
    request_motion(*input, id.globalIndex);
    return 0;
}

void free_gamepad(input_t &input, int nr) {
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    if (input->waiting[nr]) {
        input->waiting[nr] = false;
        input->roles.detach(nr);
        BOOST_LOG(info) << "PS5 virtual pad "sv << nr << " gone, never used"sv;
        return;
    }
    if (!input->pads[nr]) return;
    set_motors(*input, nr, 0, 0);
    set_triggers(*input, nr, prospero::triggers_off);
    const int rc = ps_pad_close(input->pads[nr]);  // sends a neutral state before removal
    input->pads[nr] = nullptr;
    const bool was_host = input->roles.is_host(nr);
    input->roles.detach(nr);
    input->colors[nr] = {};  // a later pad in this slot reports its colour anew
    input->triggers[nr] = {};
    input->touch[nr].clear();
    input->back[nr] = false;
    input->ps_held[nr] = false;
    BOOST_LOG(info) << "PS5 virtual pad "sv << nr << " detached ("sv << rc << ')'
                    << (was_host ? (input->roles.host_away() ? ", the host's device keeps the role"sv : ", the host role is free"sv) : ""sv);
}

void gamepad_update(input_t &input, int nr, const gamepad_state_t &state) {
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    if (input->waiting[nr]) {
        if (!prospero::wakes_pad(state.buttonFlags, state.lt, state.rt, state.lsX, state.lsY, state.rsX, state.rsY)) return;
        // This first use only brings up the console's question for the new pad; the next press answers it.
        input->waiting[nr] = false;
        if (!open_pad(*input, nr, false)) {
            input->roles.detach(nr);
            return;
        }
        request_motion(*input, nr);
    }
    if (!input->pads[nr]) return;
    const auto send = [&](uint32_t flags) {
        const auto mapped = prospero::map_gamepad(flags, state.lt, state.rt, state.lsX, state.lsY, state.rsX, state.rsY);
        const ps_pad_state pad{mapped.buttons, mapped.lx, mapped.ly, mapped.rx, mapped.ry, mapped.l2, mapped.r2};
        ps_pad_send(input->pads[nr], &pad);
    };
    // Sunshine turns a held Select into PS (back_button_timeout), but Select is Create here and pressing it
    // first would open the Create menu. So Create only goes out, as a tap, when Select comes up early. The
    // margin keeps Sunshine's own forced release at the timeout from counting as early.
    const auto role = input->roles.is_host(nr) ? prospero::PadRole::host : prospero::PadRole::guest;
    uint32_t flags = prospero::allowed_buttons(state.buttonFlags, role, input->ps_all_players);
    if ((state.buttonFlags & ~flags) && !(input->ps_held[nr]))
        BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": PS or Create ignored, only the host has them (ps_button_all_players)"sv;
    input->ps_held[nr] = state.buttonFlags & ~flags;
    bool create_tap = false;
    if (const auto timeout = config::input.back_button_timeout; timeout >= 0ms) {
        const bool back = flags & BACK;
        const auto now = std::chrono::steady_clock::now();
        if (back && !input->back[nr]) input->back_since[nr] = now;
        create_tap = !back && input->back[nr] && now - input->back_since[nr] < timeout - 20ms;
        input->back[nr] = back;
        flags &= ~BACK;
    }
    send(flags);
    if (create_tap) {
        send(flags | BACK);
        std::this_thread::sleep_for(50ms);
        send(flags);
    }
    // Sunshine neutralises held input with an all-zero state when a session ends (P4.3).
    const bool active = state.buttonFlags || state.lt || state.rt || state.lsX || state.lsY || state.rsX || state.rsY;
    if (input->active[nr] && !active) BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " released to neutral"sv;
    input->active[nr] = active;
}

void gamepad_touch(input_t &input, const gamepad_touch_t &touch) {
    const int nr = touch.id.globalIndex;
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    if (!input->pads[nr]) return;
    auto &fingers = input->touch[nr];
    // Sunshine sends this when a session ends and the pad stays. A last motion sample left in place would keep turning a
    // game's camera; a client sending it mid-stream only loses the 10 ms to its next sample.
    if (touch.eventType == prospero::moonlight::touch_cancel_all) {
        fingers.clear();
        ps_pad_rest(input->pads[nr]);
        return;
    }
    if (!fingers.update(touch.eventType, touch.pointerId, touch.x, touch.y)) return;
    std::array<ps_pad_touch, 2> points{};
    for (int i = 0; i < fingers.count(); i++) {
        const auto &point = fingers.points()[i];
        points[i] = {point.id, point.x, point.y};
    }
    ps_pad_set_touch(input->pads[nr], fingers.count(), points.data());
}

void gamepad_motion(input_t &input, const gamepad_motion_t &motion) {
    const int nr = motion.id.globalIndex;
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    if (!input->pads[nr]) return;
    if (motion.motionType == prospero::moonlight::motion_accel) {
        const float xyz[3] = {prospero::accel_to_g(motion.x), prospero::accel_to_g(motion.y), prospero::accel_to_g(motion.z)};
        ps_pad_set_acceleration(input->pads[nr], xyz);
    } else if (motion.motionType == prospero::moonlight::motion_gyro) {
        const float xyz[3] = {prospero::gyro_to_rad(motion.x), prospero::gyro_to_rad(motion.y), prospero::gyro_to_rad(motion.z)};
        ps_pad_set_angular_velocity(input->pads[nr], xyz);
    }
}

std::vector<supported_gamepad_t> &supported_gamepads(input_t *) {
    static std::vector<supported_gamepad_t> pads{{"ds5", true, ""}};
    return pads;
}

platform_caps::caps_t get_capabilities() { return platform_caps::controller_touch; }

// Sunshine keeps one of these per stream session, and keeps it for a reconnect like its pads. The keyboard and mouse
// go when the last one does, so the console doesn't list them (nor games show their prompts) once nobody streams.
class client_session: public client_input_t {
public:
    explicit client_session(input_raw_t *input): input_(input) {
        std::lock_guard lock(input_->mutex);
        ++input_->sessions;
    }

    ~client_session() override {
        std::lock_guard lock(input_->mutex);
        if (--input_->sessions == 0) close_keyboard_mouse(*input_);
    }

private:
    input_raw_t *input_;
};

std::unique_ptr<client_input_t> allocate_client_input_context(input_t &input) {
    return std::make_unique<client_session>(input.get());
}

// Each device is added on first use; one that fails is not tried again until the sessions end. The system drops what
// a device reports right after it was added (a key at 0 ms was lost, at 50 ms taken, kbm_probe on 13.00), so the
// first report waits a little, once per session.
constexpr auto device_settle = 150ms;

// A key or button let go within a few ms of going down is not seen: the first key after the settle wait above went
// down and up 1 ms apart and was lost. So a release waits until its press has been in effect this long.
constexpr auto min_hold = 30ms;

static void hold_from(std::chrono::steady_clock::time_point down) { std::this_thread::sleep_until(down + min_hold); }

static ps_keyboard *keyboard_of(input_raw_t &input) {
    if (!input.keyboard && !input.keyboard_failed) {
        if (const int rc = ps_keyboard_open(input.user, &input.keyboard); rc) {
            BOOST_LOG(error) << "PS5 virtual keyboard failed: "sv << rc;
            input.keyboard_failed = true;
        } else {
            BOOST_LOG(info) << "PS5 virtual keyboard attached"sv;
            std::this_thread::sleep_for(device_settle);
        }
    }
    return input.keyboard;
}

static ps_mouse *mouse_of(input_raw_t &input) {
    if (!input.mouse && !input.mouse_failed) {
        if (const int rc = ps_mouse_open(input.user, &input.mouse); rc) {
            BOOST_LOG(error) << "PS5 virtual mouse failed: "sv << rc;
            input.mouse_failed = true;
        } else {
            BOOST_LOG(info) << "PS5 virtual mouse attached"sv;
            std::this_thread::sleep_for(device_settle);
        }
    }
    return input.mouse;
}

static void send_mouse(input_raw_t &input, int dx, int dy, int8_t wheel = 0, int8_t tilt = 0) {
    ps_mouse *mouse = mouse_of(input);
    if (!mouse) return;
    // A large jump goes out in several reports rather than wrapping around.
    do {
        const auto x = static_cast<int16_t>(std::clamp(dx, -32767, 32767));
        const auto y = static_cast<int16_t>(std::clamp(dy, -32767, 32767));
        ps_mouse_send(mouse, x, y, input.mouse_buttons, wheel, tilt);
        dx -= x;
        dy -= y;
        wheel = tilt = 0;
    } while (dx || dy);
}

void move_mouse(input_t &input, int deltaX, int deltaY) {
    std::lock_guard lock(input->mutex);
    input->pointer.reset();
    // Sunshine nudges the pointer by a pixel and back at every stream start; that alone does not add a mouse.
    if (!input->mouse && std::abs(deltaX) <= 1 && std::abs(deltaY) <= 1) return;
    send_mouse(*input, deltaX, deltaY);
}

void move_mouse(input_t &input, const touch_port_t &, int deltaX, int deltaY) { move_mouse(input, deltaX, deltaY); }

// The console's pointer is relative only, so a client in absolute mode moves it by the difference to its last position.
void abs_mouse(input_t &input, const touch_port_t &, float x, float y) {
    std::lock_guard lock(input->mutex);
    const auto last = std::exchange(input->pointer, std::pair{x, y});
    if (!last) return;
    send_mouse(*input, static_cast<int>(std::lround(x - last->first)), static_cast<int>(std::lround(y - last->second)));
}

void button_mouse(input_t &input, int button, bool release) {
    const uint8_t bit = prospero::mouse_button_bit(button);
    if (!bit) return;
    std::lock_guard lock(input->mutex);
    const uint8_t buttons = release ? input->mouse_buttons & ~bit : input->mouse_buttons | bit;
    if (buttons == input->mouse_buttons) return;
    input->mouse_buttons = buttons;
    if (release) hold_from(input->button_down_at);
    send_mouse(*input, 0, 0);
    if (!release) input->button_down_at = std::chrono::steady_clock::now();
}

void scroll(input_t &input, int distance) {
    std::lock_guard lock(input->mutex);
    if (const int8_t steps = input->wheel.add(distance)) send_mouse(*input, 0, 0, steps, 0);
}

void hscroll(input_t &input, int distance) {
    std::lock_guard lock(input->mutex);
    if (const int8_t steps = input->tilt.add(distance)) send_mouse(*input, 0, 0, 0, steps);
}

void keyboard_update(input_t &input, uint16_t modcode, bool release, uint8_t, bool) {
    std::lock_guard lock(input->mutex);
    if (!input->keys.update(modcode, release)) return;
    ps_keyboard *keyboard = keyboard_of(*input);
    if (!keyboard) return;
    if (release) hold_from(input->key_down_at);
    ps_keyboard_send(keyboard, input->keys.modifiers(), input->keys.usages(), input->keys.count());
    if (!release) input->key_down_at = std::chrono::steady_clock::now();
}

// Text from a client's on-screen keyboard: printable ASCII is typed key by key as on a US keyboard, and what the
// client holds stays held. Other characters have no key and are dropped.
void unicode(input_t &input, const char *utf8, int size) {
    std::lock_guard lock(input->mutex);
    ps_keyboard *keyboard = keyboard_of(*input);
    if (!keyboard) return;
    const auto &held = input->keys;
    int dropped = 0;
    for (int i = 0; i < size; i++) {
        const auto key = prospero::ascii_key(utf8[i]);
        if (!key.usage) {
            dropped += (static_cast<uint8_t>(utf8[i]) & 0xC0) != 0x80;  // a UTF-8 sequence counts once
            continue;
        }
        std::array<uint8_t, prospero::KeyboardState::max_keys> usages{};
        const auto count = std::min<uint8_t>(held.count(), prospero::KeyboardState::max_keys - 1);
        std::copy_n(held.usages(), count, usages.begin());
        usages[count] = key.usage;
        const uint8_t modifiers = held.modifiers() | (key.shift ? 0x02 : 0);
        ps_keyboard_send(keyboard, modifiers, usages.data(), count + 1);
        std::this_thread::sleep_for(min_hold);
        ps_keyboard_send(keyboard, held.modifiers(), held.usages(), held.count());
        std::this_thread::sleep_for(min_hold);
    }
    if (dropped) BOOST_LOG(debug) << "PS5 virtual keyboard: "sv << dropped << " characters without a key"sv;
}

void touch_update(client_input_t *, const touch_port_t &, const touch_input_t &) {}
void pen_update(client_input_t *, const touch_port_t &, const pen_input_t &) {}
void gamepad_battery(input_t &, const gamepad_battery_t &) {}
}
