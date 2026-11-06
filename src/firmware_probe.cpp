// SPDX-License-Identifier: GPL-3.0-only
// The firmware probe: in one run of a few minutes, everything that porting the host to firmware 10.20 had to find out,
// for a firmware nobody has measured yet (docs/firmware-probe.md). It runs only when the user sends it to the ELF loader,
// never while the host streams (it stops right away then), and uploads nothing: the user reads the report and decides
// what to share. Run it with no game starting or ending, since that takes the capture away. A running host is asked to
// quit first and is started again at the end (host_control.hpp: the installed one, or the ELF named in
// /data/prosperosunshine/host_restart.next).
// Nothing personal goes into the text files: the probe reads no MAC or IP address, account or user name, console ID,
// serial or the host's sunshine.conf (it holds the name the user gave the host), says "a game or app" or "the home
// screen" rather than which title is in front, and masks anything shaped like an IPv4 or MAC address anyway, so the
// report can be posted as it is.
// Before each call that may hang the console, a line goes to detail.log (on disk before the call) and to klog, so a
// hang still shows where it happened.
// Results in /data/prosperosunshine/firmware_probe/:
//   report.txt    findings, with the codes behind them, and what the host would need on this firmware (rewritten at
//                 each phase, so a run that never ended still leaves what it found)
//   detail.log    one line per attempt, and the records as hex
//   capture.log   the capture steps from capture.c (the last 128 KiB)
//   progress.txt  the phase running now, then "done"
//   samples/      only when the user opts in with an empty file /data/prosperosunshine/firmware_probe.samples (used up
//                 by that run): short video and audio samples to decode on a PC. They hold what the screen shows and
//                 the console plays during the run, the user's avatar and name on the home screen included, so they
//                 stay on the console and out of public issues, and go to a developer only when one asks for them.
// A one-line file /data/prosperosunshine/firmware_probe.phases with phase names separated by spaces runs only those.
#include "host_control.hpp"
#include "probe_scrub.hpp"
#include "platform/ps5/capture.h"
#include "platform/ps5/diag.h"
#include "platform/ps5/probe_record.h"
#include "platform/ps5/ra_resolve.h"
#include <sys/stat.h>
#include <x86intrin.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>
extern "C" {
#include <ps5/kernel.h>
#include <ps5/klog.h>
int sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t app_id, char *title_id);
}

namespace {

using steady = std::chrono::steady_clock;
const char *const kDir = "/data/prosperosunshine/firmware_probe";
const char *const kSamples = "/data/prosperosunshine/firmware_probe/samples";
const char *const kSamplesOptIn = "/data/prosperosunshine/firmware_probe.samples";
const steady::time_point kStart = steady::now();
FILE *detail;
std::string report;
std::string phases_wanted;
double tsc_per_second;
bool keep_samples;  // the user opted in with kSamplesOptIn

long elapsed_ms() { return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(steady::now() - kStart).count()); }

void vwrite(bool to_report, const char *format, va_list args) {
    static char text[8192];
    vsnprintf(text, sizeof text, format, args);
    const std::string line = probe::scrub(text);
    if (detail) {
        fprintf(detail, "[%6ld] %s\n", elapsed_ms(), line.c_str());
        fflush(detail);
    }
    if (to_report) {
        report += line;
        report += '\n';
    }
}

// One line in detail.log.
void note(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vwrite(false, format, args);
    va_end(args);
}

// One line in report.txt, and in detail.log.
void out(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vwrite(true, format, args);
    va_end(args);
}

// Before a call that may hang the console: a line in detail.log that is on disk before the call, and in klog.
void before(const char *format, ...) {
    static char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    const std::string line = probe::scrub(text);
    if (detail) {
        fprintf(detail, "[%6ld] -> %s\n", elapsed_ms(), line.c_str());
        fflush(detail);
        fsync(fileno(detail));
    }
    klog_printf("ProsperoSunshine: firmware_probe -> %s\n", line.c_str());
}

void write_file(const char *name, const std::string &text) {
    const std::string path = std::string(kDir) + "/" + name;
    if (FILE *f = fopen(path.c_str(), "w")) {
        fwrite(text.data(), 1, text.size(), f);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
    }
}

void progress(const char *phase) {
    char text[128];
    snprintf(text, sizeof text, "%s %ld ms\n", phase, elapsed_ms());
    write_file("progress.txt", text);
    // What the run found so far, in case it never ends.
    write_file("report.txt", report + "\n(unfinished: the run was in phase " + phase +
                                 "; the last line of detail.log is the last call it made)\n");
    note("== phase %s", phase);
    klog_printf("ProsperoSunshine: firmware_probe phase %s\n", phase);
}

bool wanted(const char *phase) {
    if (phases_wanted.empty()) return true;
    const std::string padded = " " + phases_wanted + " ";
    return padded.find(std::string(" ") + phase + " ") != std::string::npos;
}

// A phase that runs only when the file names it.
bool named(const char *phase) { return !phases_wanted.empty() && wanted(phase); }

std::string hex(const uint8_t *p, size_t size) {
    std::string s;
    char b[4];
    for (size_t i = 0; i < size; i++) {
        snprintf(b, sizeof b, "%02x", p[i]);
        s += b;
        if (i % 32 == 31 && i + 1 < size) s += ' ';
    }
    return s;
}

uint32_t get32(const uint8_t *p, size_t at) {
    uint32_t v;
    memcpy(&v, p + at, 4);
    return v;
}

uint64_t get64(const uint8_t *p, size_t at) {
    uint64_t v;
    memcpy(&v, p + at, 8);
    return v;
}

// --- The host and the console --------------------------------------------------------------------------------------

// The title ID in front, "" on the home screen. Only compared, never written: the report says in_front() instead.
std::string title_in_front() {
    char title[16] = {};
    const int app = sceLncUtilGetAppIdOfRunningBigApp();
    if (app <= 0 || sceLncUtilGetAppTitleId(static_cast<uint32_t>(app), title)) return "";
    return title;
}

const char *in_front(const std::string &title) { return title.empty() ? "the home screen" : "a game or app"; }
std::string title_at_start;

uint32_t module_handle(const char *name) {
    uint32_t handle = 0;
    return kernel_dynlib_handle(-1, name, &handle) == 0 ? handle : 0;
}

intptr_t libkernel_symbol(const char *name) {
    for (const char *lib : {"libkernel_web.sprx", "libkernel_sys.sprx", "libkernel.sprx"})
        if (const uint32_t h = module_handle(lib))
            if (const intptr_t a = kernel_dynlib_dlsym(-1, h, name)) return a;
    return 0;
}

// --- One capture attempt -------------------------------------------------------------------------------------------

using Words = std::vector<std::pair<size_t, uint32_t>>;

struct Hook {
    bool rewrite = false;
    ps_probe_layout layout{};
    Words words;
    bool called = false;
    ptrdiff_t shift = 0;
    uint32_t video_out_mode = 0;
    size_t built = 0;
    uint8_t seen[PS_CAPTURE_RECORD_CAPACITY]{};  // what configure() wrote
    uint8_t sent[PS_CAPTURE_RECORD_CAPACITY]{};  // what went to OpenVideo
} hook;

void record_hook(uint8_t *record, size_t capacity, ptrdiff_t shift, void *) {
    hook.called = true;
    hook.shift = shift;
    memcpy(hook.seen, record, capacity);
    hook.video_out_mode = get32(record, static_cast<size_t>(0x2a0 + shift));
    if (hook.rewrite) {
        ps_probe_layout l = hook.layout;
        l.video_out_mode = hook.video_out_mode;
        hook.built = ps_probe_build_record(record, capacity, &l);
    }
    for (const auto &w : hook.words) memcpy(record + w.first, &w.second, 4);
    memcpy(hook.sent, record, capacity);
}

struct Try {
    std::string label;
    bool rewrite = false;          // send `layout` instead of configure()'s record
    ps_probe_layout layout{};
    Words words;                   // 32-bit writes after that
    uint32_t codec = 0, resolution = 4, timing = 1, ten_bit = 0;
    int resource = 0;              // 0 = what the mode picks (180, from 1440p 182)
    uint32_t average = 6000000, peak = 6720000;
    bool open_only = false;
    int attempts = 1;              // OpenVideo tries while it answers 0x81950008; 0 = the host's
    int read_ms = 1000;
    int idr_every = 0;             // RequestKeyFrame every n frames
    const char *save = nullptr;    // stream file in kDir
    int bitrate_at_ms = 0;         // SetVideoBitrate this far into the read
    uint32_t bitrate_to = 0;       // to this average (peak +10 %); 0 = the opening one
    bool dump_descriptors = false;
};

struct Result {
    int rc = 0, raw = 0;
    std::string step;
    int frames = 0, idrs = 0, first_idr_frame = -1, idr_gap = 0, largest = 0, first_size = 0, header_bytes = 0;
    long bytes = 0;
    int slices_min = 0, slices_max = 0, slices_sum = 0;
    int read_rc = 0, read_raw = 0;
    double tsc_fps = 0;
    int bitrate_rc = 1, frames_after_bitrate = 0;
    bool opened() const { return rc == 0; }
    long kbit(int ms) const { return ms > 0 ? bytes * 8 / ms : 0; }
};

// Slices and IDRs in one frame. H.264: VCL types 1 and 5 (5 = IDR). HEVC: VCL types 0..31 (19, 20 = IDR).
void scan_nal(const uint8_t *data, int size, bool hevc, int &slices, bool &idr) {
    slices = 0;
    idr = false;
    for (int i = 0; i + 3 < size; i++) {
        if (data[i] || data[i + 1] || data[i + 2] != 1) continue;
        const int type = hevc ? (data[i + 3] >> 1) & 0x3f : data[i + 3] & 0x1f;
        if (hevc ? type < 32 : type == 1 || type == 5) ++slices;
        if (hevc ? type == 19 || type == 20 : type == 5) idr = true;
        i += 2;
    }
}

std::vector<uint8_t> buffer(16 * 1024 * 1024);

// Some failed opens leave the resource listed for a while (on 10.20, OpenVideo answering 0x80c70010 to a value at 0x1f8
// kept 180 listed for about 30 s), and every open in that time fails at "resource list". Each attempt waits for it, and
// the report says which attempt left it behind. Past kBusyBudget seconds in all, the capture counts as taken (a stream?).
constexpr int kBusyWaitSeconds = 45, kBusyBudget = 180;
int busy_waited_total;
bool capture_taken;
std::string last_label, lingering;

Result run(const Try &t) {
    Result r;
    hook.rewrite = t.rewrite;
    hook.layout = t.layout;
    hook.words = t.words;
    hook.called = false;
    hook.built = 0;
    ps_capture_set_codec(t.codec);
    ps_capture_set_ten_bit(t.ten_bit);
    ps_capture_set_video_mode(t.resolution, t.timing);
    if (t.resource) ps_capture_set_resource(t.resource);
    ps_capture_set_opening_bitrate(t.average, t.peak);
    ps_capture_set_open_only(t.open_only);
    ps_capture_set_open_attempts(t.attempts);
    ps_capture *s = nullptr;
    int waited = 0;
    if (!capture_taken) before("%s", t.label.c_str());
    for (;;) {
        r.rc = capture_taken ? PS_CAPTURE_BUSY : ps_capture_open(&s);
        if (r.rc != PS_CAPTURE_BUSY || capture_taken || waited >= kBusyWaitSeconds) break;
        if (busy_waited_total >= kBusyBudget) {
            capture_taken = true;
            out("  capture still listed as busy after %d s of waiting in all: the remaining capture attempts are skipped", busy_waited_total);
            break;
        }
        sleep(1);
        ++waited;
        ++busy_waited_total;
    }
    if (waited && r.rc != PS_CAPTURE_BUSY) {
        char text[256];
        snprintf(text, sizeof text, "  %d s after \"%s\"", waited, last_label.c_str());
        lingering += std::string(text) + "\n";
        note("%s: waited %d s for the resource, left listed after %s", t.label.c_str(), waited, last_label.c_str());
    }
    last_label = t.label;
    if (r.rc) {
        const char *step = "";
        r.raw = ps_capture_last_failure(&step);
        r.step = step;
        note("%s: %s rc=%d step=%s raw=%#x", t.label.c_str(), t.open_only && r.step == "open only" ? "OPENED (open only)" : "failed",
             r.rc, r.step.c_str(), static_cast<unsigned>(r.raw));
        usleep(100000);
        return r;
    }
    const bool hevc = t.codec == 1;
    FILE *f = nullptr;
    if (t.save && keep_samples) {
        const std::string path = std::string(kSamples) + "/" + t.save;
        f = fopen(path.c_str(), "wb");
    }
    r.header_bytes = ps_capture_headers(s, buffer.data(), buffer.size());
    if (f && r.header_bytes > 0) fwrite(buffer.data(), 1, static_cast<size_t>(r.header_bytes), f);
    // No RequestKeyFrame here: the encoder starts with an IDR, and the gap to the next one is the periodic interval.
    uint8_t desc[0x88];
    uint64_t tsc_first = 0, tsc_last = 0;
    int tsc_frames = 0, last_idr = -1;
    r.slices_min = 1 << 30;
    const steady::time_point begin = steady::now();
    bool bitrate_done = false;
    for (;;) {
        const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(steady::now() - begin).count());
        if (ms >= t.read_ms) break;
        if (t.bitrate_at_ms && !bitrate_done && ms >= t.bitrate_at_ms) {
            bitrate_done = true;
            const uint32_t average = t.bitrate_to ? t.bitrate_to : t.average, peak = t.bitrate_to ? t.bitrate_to / 10 * 11 : t.peak;
            before("%s: SetVideoBitrate %u/%u", t.label.c_str(), average, peak);
            r.bitrate_rc = ps_capture_set_bitrate(s, average, peak);
            note("%s: SetVideoBitrate %u/%u rc=%d after %d frames", t.label.c_str(), average, peak, r.bitrate_rc, r.frames);
        }
        memset(desc, 0, sizeof desc);
        const int rc = hevc ? ps_capture_read2(s, buffer.data(), buffer.size(), desc)
                            : ps_capture_read(s, buffer.data(), buffer.size(), desc);
        if (rc < 0) {
            r.read_rc = rc;
            r.read_raw = ps_capture_last_failure(nullptr);
            break;
        }
        if (rc == 0) {
            usleep(1000);
            continue;
        }
        ++r.frames;
        if (bitrate_done) ++r.frames_after_bitrate;
        r.bytes += rc;
        if (rc > r.largest) r.largest = rc;
        if (r.frames == 1) r.first_size = rc;
        if (f) fwrite(buffer.data(), 1, static_cast<size_t>(rc), f);
        int slices;
        bool idr;
        scan_nal(buffer.data(), rc, hevc, slices, idr);
        r.slices_sum += slices;
        r.slices_min = std::min(r.slices_min, slices);
        r.slices_max = std::max(r.slices_max, slices);
        if (idr) {
            ++r.idrs;
            if (r.first_idr_frame < 0) r.first_idr_frame = r.frames;
            if (last_idr >= 0 && !r.idr_gap && !t.idr_every) r.idr_gap = r.frames - last_idr;
            last_idr = r.frames;
        }
        // Capture time in TSC ticks: descriptor +0x48 (ReadVideo), +0x58 (ReadVideo2) (docs/avcap2-descriptor.md).
        const uint64_t tsc = get64(desc, hevc ? 0x58 : 0x48);
        if (tsc) {
            if (!tsc_first) tsc_first = tsc;
            tsc_last = tsc;
            ++tsc_frames;
        }
        if (t.dump_descriptors && r.frames <= 3) note("%s: frame %d %d bytes descriptor %s", t.label.c_str(), r.frames, rc, hex(desc, hevc ? 0x88 : 0x70).c_str());
        if (t.idr_every && r.frames % t.idr_every == 0) ps_capture_request_keyframe(s);
    }
    if (f) fclose(f);
    if (r.frames == 0) r.slices_min = 0;
    if (tsc_frames > 1 && tsc_last > tsc_first && tsc_per_second > 0)
        r.tsc_fps = (tsc_frames - 1) / (static_cast<double>(tsc_last - tsc_first) / tsc_per_second);
    const int close_rc = ps_capture_close(s);
    note("%s: open ok, %d frames %ld bytes (%ld kbit/s) largest %d first %d headers %d, slices %d..%d avg %d, IDRs %d (first at %d, gap %d), "
         "TSC fps %.2f, read %d raw %#x, close %d",
         t.label.c_str(), r.frames, r.bytes, r.kbit(t.read_ms), r.largest, r.first_size, r.header_bytes, r.slices_min, r.slices_max,
         r.frames ? r.slices_sum / r.frames : 0, r.idrs, r.first_idr_frame, r.idr_gap, r.tsc_fps, r.read_rc,
         static_cast<unsigned>(r.read_raw), close_rc);
    usleep(150000);
    return r;
}

// "ok", "no-frames" or the raw code, for a table cell.
std::string cell(const Result &r) {
    if (r.opened()) return r.frames ? "ok" : "no-frames";
    char text[24];
    snprintf(text, sizeof text, "%#x", static_cast<unsigned>(r.raw));
    return text;
}

// The high halves of the record's 64-bit size headers: zero, but writing them breaks the header, not a field.
bool header_high_half(size_t at, const ps_probe_layout &l) {
    std::vector<size_t> headers = {0, 8, 0x30, 0x38, 0x50, 0x88, 0xb8};
    if (l.inner_headers) headers.insert(headers.end(), {0xc0, 0xe0, 0x100, 0x120});
    for (const size_t h : {0x1e8, 0x1f0, 0x218, 0x2a8}) headers.push_back(static_cast<size_t>(static_cast<ptrdiff_t>(h) + l.shift));
    if (l.hevc_fields) headers.insert(headers.end(), {0x60, 0x188, static_cast<size_t>(0x2b8 + l.hevc_high)});
    return std::find(headers.begin(), headers.end(), at - 4) != headers.end();
}

std::string outcome(const Result &r) {
    char text[160];
    if (!r.opened())
        snprintf(text, sizeof text, "%s %#x", r.step.c_str(), static_cast<unsigned>(r.raw));
    else
        snprintf(text, sizeof text, "%d frames, %d slices, %ld KB%s", r.frames, r.frames ? r.slices_sum / r.frames : 0, r.bytes / 1024,
                 r.read_rc ? ", read failed" : "");
    return text;
}

// --- What the host does today ---------------------------------------------------------------------------------------

ptrdiff_t host_shift;
uint32_t video_out_mode = 0x13c;

// For the summary at the end.
struct Findings {
    int host_h264 = -1, host_hevc8 = -1, host_hevc10 = -1;  // -1 not tried, 0 fails, 1 frames
    int host_slices = 0;
    long qp_none = 0, qp_fine = 0;                           // bytes without QP fields and with 20/24
    int qp_52_fails = -1;
    int bitrate_breaks = -1;
    int hevc_minimal = -1;
    std::string modes_ok;
} found;

ps_probe_layout layout_for(ptrdiff_t shift, bool inner, bool rate, uint32_t codec, uint32_t resolution, uint32_t timing,
                           uint32_t ten_bit, uint32_t average, uint32_t peak) {
    ps_probe_layout l = ps_probe_host_layout(shift, codec, resolution, timing, ten_bit, resolution >= 5 ? 182 : 180,
                                             video_out_mode, average, peak, PS_EXP_MINIMAL);
    l.inner_headers = inner;
    l.rate_fields = rate;
    return l;
}

// The layout the rest of the run builds on: the first that started.
struct Working {
    bool found = false;
    ptrdiff_t shift = 0;
    bool inner = false, rate = false;
} working;
bool layout_tried;
// The host's own combination (its shift, with inner headers and rate fields from 13.00 on) is among those that start. On
// 13.00 all four variants start and the first is taken as the working layout, so the summary asks this instead.
bool host_layout_starts;

ps_probe_layout working_layout(uint32_t codec = 0, uint32_t resolution = 4, uint32_t timing = 1, uint32_t ten_bit = 0) {
    return layout_for(working.shift, working.inner, working.rate, codec, resolution, timing, ten_bit, 6000000, 6720000);
}

Try working_try(const std::string &label) {
    Try t;
    t.label = label;
    t.rewrite = true;
    t.layout = working_layout();
    return t;
}

// The first line of a text file, "" if there is none.
std::string first_line(const char *path) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line;
}

// "contentVersion" of the app's param.json, wherever ShadowMountPlus finds the app.
std::string app_version(std::string &where) {
    for (const char *root : {"/data/homebrew", "/mnt/ext0/homebrew", "/mnt/ext1/homebrew", "/mnt/usb0/homebrew", "/mnt/usb1/homebrew"}) {
        const std::string path = std::string(root) + "/PPSA47989/sce_sys/param.json";
        std::ifstream in(path);
        const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t key = json.find("\"contentVersion\"");
        if (key == std::string::npos) continue;
        const size_t open = json.find('"', json.find(':', key) + 1), close = json.find('"', open + 1);
        if (open == std::string::npos || close == std::string::npos) continue;
        where = path;
        return json.substr(open + 1, close - open - 1);
    }
    return "";
}

void phase_environment() {
    progress("environment");
#ifndef FIRMWARE_PROBE_COMMIT
#define FIRMWARE_PROBE_COMMIT ""
#endif
    out("probe: version 2, built from %s", FIRMWARE_PROBE_COMMIT[0] ? FIRMWARE_PROBE_COMMIT : "a local tree");
    const uint32_t fw = kernel_get_fw_version();
    out("firmware %#x (%x.%02x)", fw, fw >> 24, (fw >> 16) & 0xff);
    // System software version as the system reports it: {u64 size, char string[0x1c], u32 version} (as on the PS4).
    if (const intptr_t sw = libkernel_symbol("sceKernelGetSystemSwVersion")) {
        uint8_t info[0x100] = {};
        const uint64_t size = 0x28;
        memcpy(info, &size, sizeof size);
        before("sceKernelGetSystemSwVersion");
        const int rc = reinterpret_cast<int (*)(void *)>(sw)(info);
        char text[0x1d] = {};
        memcpy(text, info + 8, 0x1c);
        bool printable = true;
        for (const char c : std::string(text)) printable = printable && c >= 0x20 && c < 0x7f;
        out("sceKernelGetSystemSwVersion: rc=%#x \"%s\" version=%#x (the homebrew environment may report another version than the kernel)",
            static_cast<unsigned>(rc), printable ? text : "?", get32(info, 0x24));
    }
    std::string where;
    const std::string app = app_version(where);
    out("ProsperoSunshine app: %s", app.empty() ? "not found under */homebrew/PPSA47989" : (app + " (" + where + ")").c_str());
    const std::string host = first_line("/data/prosperosunshine/host_diag.log");
    out("last host started (host_diag.log): %s", host.empty() ? "none" : host.c_str());
    // Has: the console is a Pro. Is: the Pro mode is on now; on a Pro with 10.20 it read 1 on the home screen and 0 with
    // a game in front.
    for (const auto &[name, meaning] : {std::pair{"sceKernelHasTrinityMode", "1 = PS5 Pro"},
                                        std::pair{"sceKernelIsTrinityMode", "1 = the Pro mode is on now; it follows the title in front"}}) {
        if (const intptr_t f = libkernel_symbol(name)) {
            before("%s", name);
            out("console: %s() = %d (%s)", name, reinterpret_cast<int (*)()>(f)(), meaning);
        } else {
            out("console: %s not exported", name);
        }
    }
    title_at_start = title_in_front();
    out("in front at start: %s", in_front(title_at_start));

    // TSC against the steady clock over 200 ms, as video_ps5.cpp calibrates it.
    const steady::time_point c0 = steady::now();
    const uint64_t t0 = __rdtsc();
    usleep(200000);
    const uint64_t t1 = __rdtsc();
    const double seconds = std::chrono::duration<double>(steady::now() - c0).count();
    tsc_per_second = static_cast<double>(t1 - t0) / seconds;
    out("TSC %.5f ticks/ns", tsc_per_second / 1e9);

    // What one klog line and one flushed file line cost here: the host logs to both.
    std::vector<long> klog_us, file_us;
    for (int i = 0; i < 20; i++) {
        const steady::time_point a = steady::now();
        klog_printf("ProsperoSunshine: firmware_probe klog timing %02d ............................................\n", i);
        klog_us.push_back(static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(steady::now() - a).count()));
    }
    for (int i = 0; i < 20; i++) {
        const steady::time_point a = steady::now();
        note("file timing %02d ...........................................................", i);
        file_us.push_back(static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(steady::now() - a).count()));
    }
    std::sort(klog_us.begin(), klog_us.end());
    std::sort(file_us.begin(), file_us.end());
    out("klog_printf: median %ld us, max %ld us; file line with fflush: median %ld us", klog_us[10], klog_us.back(), file_us[10]);
}

void phase_host() {
    progress("host");
    out("");
    out("== The host's own record (configure()) on this firmware");
    Try h264;
    h264.label = "host H.264 1080p60";
    h264.attempts = 0;
    h264.read_ms = 3000;
    h264.save = "h264_1080p60.h264";
    h264.dump_descriptors = true;
    const Result r = run(h264);
    host_shift = ps_capture_record_shift();
    if (hook.called) {
        video_out_mode = hook.video_out_mode;
        const size_t size = static_cast<size_t>(0x2e0 + host_shift);
        note("host record (%zu bytes): %s", size, hex(hook.seen, size).c_str());
        const ps_probe_layout l = ps_probe_host_layout(host_shift, 0, 4, 1, 0, 180, video_out_mode, 6000000, 6720000, PS_EXP_STANDARD);
        uint8_t mine[PS_CAPTURE_RECORD_CAPACITY];
        ps_probe_build_record(mine, sizeof mine, &l);
        // A host that sets 10.x's QP (0xc4/0xc8) writes them from the bitrate; the probe sets them per attempt.
        if (host_shift < 0) memcpy(mine + ps_qp_offset(host_shift), hook.seen + ps_qp_offset(host_shift), 8);
        size_t at = 0;
        while (at < size && mine[at] == hook.seen[at]) at++;
        out("record shift %+td (total %#zx), video out mode %#x; probe builder %s", host_shift, size, video_out_mode,
            at == size ? "matches configure()" : "DIFFERS from configure()");
        if (at != size) note("builder differs at %#zx", at);
    } else {
        out("configure() never ran: the open failed before it (step %s)", r.step.c_str());
    }
    found.host_h264 = r.opened() && r.frames > 0;
    found.host_slices = r.frames ? r.slices_sum / r.frames : 0;
    out("H.264 1080p60: %s", r.opened() ? "opens and starts" : ("fails at " + outcome(r)).c_str());
    if (r.opened())
        out("  %d frames in 3 s (TSC says %.1f fps), %d slices per frame (%d..%d), %d IDR (%s), first frame %d bytes, headers %d bytes",
            r.frames, r.tsc_fps, r.frames ? r.slices_sum / r.frames : 0, r.slices_min, r.slices_max, r.idrs,
            r.idr_gap ? ("then every " + std::to_string(r.idr_gap) + " frames").c_str() : "the first only; the host asks for one every 600 frames",
            r.first_size, r.header_bytes);
    if (r.opened() && r.slices_max > 32) out("  more than 32 slices: FFmpeg's hardware decoders (Moonlight on Windows) refuse that");

    for (const uint32_t ten : {0u, 1u}) {
        Try hevc;
        hevc.label = ten ? "host HEVC 10-bit 1080p60" : "host HEVC 8-bit 1080p60";
        hevc.codec = 1;
        hevc.ten_bit = ten;
        hevc.attempts = 0;
        hevc.peak = 6600000;
        hevc.read_ms = 2000;
        hevc.save = ten ? "hevc10_1080p60.h265" : "hevc8_1080p60.h265";
        hevc.dump_descriptors = ten == 0;
        const Result e = run(hevc);
        (ten ? found.host_hevc10 : found.host_hevc8) = e.opened() && e.frames > 0;
        out("HEVC %s-bit 1080p60: %s", ten ? "10" : "8", e.opened() ? outcome(e).c_str() : ("fails at " + outcome(e)).c_str());
    }

    // Audio over a video session, as the host opens it: PCM (what it uses) and Opus.
    ps_capture_set_codec(0);
    ps_capture_set_ten_bit(0);
    ps_capture_set_video_mode(4, 1);
    ps_capture_set_open_attempts(0);
    ps_capture_set_open_only(0);
    hook.rewrite = false;
    hook.words.clear();
    ps_capture *s = nullptr;
    before("audio: video session to open it on");
    if (ps_capture_open(&s)) {
        out("audio: no video session to open it on");
        return;
    }
    for (const uint32_t encoder : {0u, 2u}) {
        before("audio %s: OpenAudio", encoder ? "Opus" : "PCM");
        const int rc = ps_capture_audio_open_ex(s, encoder, encoder ? 96000 : 0, 0, 2, 180, 0);
        if (rc) {
            out("audio %s: OpenAudio %#x", encoder ? "Opus" : "PCM", static_cast<unsigned>(rc));
            continue;
        }
        const std::string path = std::string(kSamples) + (encoder ? "/audio_opus.bin" : "/audio_pcm_f32le_2ch.raw");
        FILE *f = keep_samples ? fopen(path.c_str(), "wb") : nullptr;
        uint8_t header[0x10];
        int packets = 0, smallest = 1 << 30, biggest = 0, error = 0;
        long bytes = 0;
        std::string first_header;
        const steady::time_point begin = steady::now();
        while (steady::now() - begin < std::chrono::seconds(2)) {
            const int n = ps_capture_audio_read(s, buffer.data(), buffer.size(), header);
            if (n < 0) {
                error = n;
                break;
            }
            if (n == 0) {
                usleep(500);
                continue;
            }
            if (!packets) first_header = hex(header, sizeof header);
            ++packets;
            bytes += n;
            smallest = std::min(smallest, n);
            biggest = std::max(biggest, n);
            if (f) fwrite(buffer.data(), 1, static_cast<size_t>(n), f);
        }
        if (f) fclose(f);
        ps_capture_audio_close(s);
        out("audio %s: %d packets in 2 s (%d..%d bytes, %ld bytes/s), first header %s%s", encoder ? "Opus 96 kbit/s" : "PCM", packets,
            packets ? smallest : 0, biggest, bytes / 2, first_header.c_str(), error ? ", read failed" : "");
        if (!encoder && packets) out("  PCM at 48 kHz float stereo would be 384000 bytes/s");
    }
    ps_capture_close(s);
}

// The 59 exports of libSceAvcap2 and their offsets on 13.00 (docs/re/avcap2-exports.md): which moved, which are gone.
const std::pair<const char *, uint32_t> kAvcap2Exports[] = {
    {"svzPXluOz8U", 0xD0},   {"gojqghDU+1Y", 0x130},  {"3nHpE7Dp5SE", 0x190},  {"4jwUgirLFrA", 0x360},  {"Tzk7-W5mp0A", 0x730},
    {"ijHDdN-xBf4", 0x8C0},  {"tR7gPe1i8hw", 0xA50},  {"8CGNCwBsItI", 0xB40},  {"Z5dKK0xnQ8g", 0xBD0},  {"WhsHggqdwcg", 0xC60},
    {"pN0hIQZsMzc", 0xDD0},  {"jkv9MlN7gvs", 0xF00},  {"TyUnv8GHF0o", 0xF10},  {"1Xp85wUaVTE", 0x10E0}, {"8j-pBJmjL7M", 0x1170},
    {"ysFbdZrSCMM", 0x1200}, {"S4nvYDibKBw", 0x1340}, {"BjHu5q5-6NI", 0x13D0}, {"NeKlrUv+lbY", 0x1450}, {"VAFgNc0Pfp8", 0x14F0},
    {"8HjqtZXLvek", 0x1580}, {"aAbMwQwWHyM", 0x1610}, {"Cdf6VsJgvaY", 0x16B0}, {"RW88AiwmL+Y", 0x1750}, {"YaXDckFygvw", 0x17D0},
    {"DHFDHdKAr9U", 0x1860}, {"HAeczEM-m7c", 0x18E0}, {"88YEF+XHfRY", 0x1970}, {"Dht3tcbRoEY", 0x1A10}, {"CYgd06bO5q0", 0x1AB0},
    {"yN0w4tWivqs", 0x1B90}, {"96UQhNnBlxc", 0x1C60}, {"B+6003nL0AI", 0x1D30}, {"j7ClJpPRVQI", 0x1DC0}, {"NEY5WpUI0Io", 0x1E70},
    {"117ggZLcGc0", 0x1F00}, {"Ez6n05jUI1U", 0x1F80}, {"-NotZDC2pQc", 0x2010}, {"fLzv9I4mHF0", 0x20B0}, {"iy6ZcsdeoMk", 0x2140},
    {"mYgFfxPwEjY", 0x21E0}, {"4S4mGtzLmyI", 0x2280}, {"41+5ixaiTj0", 0x2310}, {"BPgql5PsMKo", 0x23B0}, {"32gujAUoIMc", 0x2440},
    {"k7gz608olKM", 0x24D0}, {"7KBaem5GyBQ", 0x2540}, {"X8c-f8LgzHo", 0x25F0}, {"2GC2UnBJHIA", 0x26A0}, {"0W6GB-ecnK0", 0x2740},
    {"2Lv-4HVkqO4", 0x27C0}, {"ImOtOnPjgio", 0x2840}, {"4hkuDvkGu7U", 0x28E0}, {"HrGLkn9o4MY", 0x2950}, {"VNRuuZQx7gE", 0x29C0},
    {"pEm2jVo44dE", 0x2A40}, {"r9BQzY0IvxA", 0x2AC0}, {"GBx9f5BmDvU", 0x2B30}, {"+OspdnZ-8mc", 0x2BA0},
};

// The 39 names known for those NIDs (docs/re/avcap2-exports.md), without the "sceAvcap2" prefix.
const char *export_name(const char *nid) {
    static const std::pair<const char *, const char *> names[] = {
        {"svzPXluOz8U", "Initialize"}, {"gojqghDU+1Y", "Terminate"}, {"3nHpE7Dp5SE", "OpenAudio"}, {"4jwUgirLFrA", "OpenVideo"},
        {"Tzk7-W5mp0A", "OpenHaptic"}, {"ijHDdN-xBf4", "OpenPadSpk"}, {"tR7gPe1i8hw", "Close"}, {"8CGNCwBsItI", "Start"},
        {"Z5dKK0xnQ8g", "Stop"}, {"WhsHggqdwcg", "ReadAudio"}, {"pN0hIQZsMzc", "ReadVideo"}, {"jkv9MlN7gvs", "ReadVideo2"},
        {"TyUnv8GHF0o", "ReadVideo3"}, {"1Xp85wUaVTE", "ReadHaptic"}, {"8j-pBJmjL7M", "ReadPadSpk"}, {"ysFbdZrSCMM", "Select"},
        {"NeKlrUv+lbY", "GetNonVclNalUnitStream"}, {"VAFgNc0Pfp8", "GetNonVclNalUnitStream2"},
        {"8HjqtZXLvek", "GetNonVclNalUnitStream3"}, {"Cdf6VsJgvaY", "SetImagePaste"}, {"YaXDckFygvw", "RequestKeyFrame"},
        {"DHFDHdKAr9U", "GetVideoOutMode"}, {"CYgd06bO5q0", "AllocateVideoEncoderMemory"}, {"96UQhNnBlxc", "FreeEncoderVideoMemory"},
        {"j7ClJpPRVQI", "GetImagePasteMemory"}, {"NEY5WpUI0Io", "ChangeScanInResolution"}, {"117ggZLcGc0", "GetVideoEncoderCapability"},
        {"Ez6n05jUI1U", "RequestSkipFrame"}, {"-NotZDC2pQc", "SetVideoEncodeConfig"}, {"fLzv9I4mHF0", "SetInvalidFrame"},
        {"iy6ZcsdeoMk", "SetVideoBitrate"}, {"mYgFfxPwEjY", "GetAudioOutVolume"}, {"4S4mGtzLmyI", "SetAudioOutVolume"},
        {"41+5ixaiTj0", "GetMicVolume"}, {"BPgql5PsMKo", "SetMicVolume"}, {"X8c-f8LgzHo", "MapMemory"}, {"ImOtOnPjgio", "SetPrivacyImage"},
        {"4hkuDvkGu7U", "DisableScanInEncode"}, {"HrGLkn9o4MY", "EnableScanInEncode"},
    };
    for (const auto &n : names)
        if (!strcmp(n.first, nid)) return n.second;
    return "unnamed";
}

void phase_symbols() {
    progress("symbols");
    out("");
    out("== System libraries");
    if (const uint32_t av = module_handle("libSceAvcap2.sprx")) {
        const intptr_t base = kernel_dynlib_mapbase_addr(-1, av);
        int same = 0, moved = 0, missing = 0;
        std::string moves, missing_names;
        for (const auto &e : kAvcap2Exports) {
            const intptr_t a = kernel_dynlib_resolve(-1, av, e.first);
            if (!a) {
                ++missing;
                missing_names += std::string(" ") + e.first + " (" + export_name(e.first) + ")";
                note("avcap2 %s missing", e.first);
                continue;
            }
            const uint64_t off = static_cast<uint64_t>(a - base);
            note("avcap2 %s at %#llx (13.00: %#x)", e.first, static_cast<unsigned long long>(off), e.second);
            if (off == e.second) {
                ++same;
            } else {
                ++moved;
                if (moved <= 8) {
                    char m[48];
                    snprintf(m, sizeof m, " %s:%#llx", e.first, static_cast<unsigned long long>(off));
                    moves += m;
                }
            }
        }
        out("libSceAvcap2: %d of 59 exports at 13.00's offsets, %d elsewhere, %d missing%s%s", same, moved, missing,
            moved ? "; first moved:" : "", moves.c_str());
        if (missing) out("  missing:%s", missing_names.c_str());
        if (const intptr_t cap = kernel_dynlib_dlsym(-1, av, "sceAvcap2GetVideoEncoderCapability")) {
            uint32_t value = 0;
            before("sceAvcap2GetVideoEncoderCapability");
            const int rc = reinterpret_cast<int (*)(uint32_t *)>(cap)(&value);
            out("sceAvcap2GetVideoEncoderCapability rc=%#x value=%#x", static_cast<unsigned>(rc), value);
        }
        if (const intptr_t mode = kernel_dynlib_dlsym(-1, av, "sceAvcap2GetVideoOutMode")) {
            int value = 0;
            before("sceAvcap2GetVideoOutMode");
            const int rc = reinterpret_cast<int (*)(int *)>(mode)(&value);
            out("sceAvcap2GetVideoOutMode outside a session rc=%#x value=%#x (inside one the host read %#x; bits: 4 HDR, 8 4K scan-in, 0x10 60 Hz, 0x20 1440p, 0x100 4K)",
                static_cast<unsigned>(rc), static_cast<unsigned>(value), video_out_mode);
        }
    } else {
        out("libSceAvcap2 not loaded: the capture never got that far");
    }
    if (const uint32_t ra = module_handle("libSceResourceArbitrator.sprx")) {
        ps_ra_functions f{};
        before("ResourceArbitrator: resolve");
        const int rc = ps_ra_resolve(ra, &f);
        out("ResourceArbitrator: resolve %s, firmware %s; by NID register=%#x unregister=%#x request=%#x list=%#x; used %#x %#x %#x %#x",
            rc ? "REFUSED" : "ok", f.known_firmware ? "known" : "not in the table", f.nid[PS_RA_REGISTER], f.nid[PS_RA_UNREGISTER],
            f.nid[PS_RA_REQUEST], f.nid[PS_RA_LIST], f.offset[PS_RA_REGISTER], f.offset[PS_RA_UNREGISTER], f.offset[PS_RA_REQUEST],
            f.offset[PS_RA_LIST]);
        if (!rc && f.base) {
            int32_t active[32] = {};
            before("ResourceArbitrator: list");
            const int list_rc = reinterpret_cast<int (*)(int32_t *)>(f.base + f.offset[PS_RA_LIST])(active);
            std::string ids;
            for (const int32_t id : active)
                if (id > 0) ids += " " + std::to_string(id);
            out("ResourceArbitrator list rc=%#x, active:%s (the host asks for 180, or 182 above 1080p)", static_cast<unsigned>(list_rc),
                ids.c_str());
        }
    } else {
        out("libSceResourceArbitrator not loaded");
    }
}

// Which record layouts the validator takes (open only), then which of those start and deliver frames.
void phase_layout() {
    progress("layout");
    layout_tried = true;
    out("");
    out("== Record layout: OpenVideo for each size, with and without 13.00's inner headers and rate fields (open only)");
    struct Accepted {
        ptrdiff_t shift;
        bool inner, rate;
    };
    std::vector<Accepted> accepted;
    std::string table;
    for (ptrdiff_t shift = -0x40; shift <= 0x20; shift += 8) {
        char row[160];
        snprintf(row, sizeof row, "  %+4td (%#5tx):", shift, 0x2e0 + shift);
        std::string line = row;
        for (const bool inner : {false, true})
            for (const bool rate : {false, true}) {
                Try t;
                snprintf(row, sizeof row, "layout %+td inner=%d rate=%d", shift, inner, rate);
                t.label = row;
                t.rewrite = true;
                t.layout = layout_for(shift, inner, rate, 0, 4, 1, 0, 6000000, 6720000);
                t.open_only = true;
                const Result r = run(t);
                const bool ok = !r.opened() && r.step == "open only";
                if (ok) accepted.push_back({shift, inner, rate});
                if (ok) snprintf(row, sizeof row, " %s%s=ok", inner ? "I" : "-", rate ? "R" : "-");
                else if (r.step == "OpenVideo") snprintf(row, sizeof row, " %s%s=%02x", inner ? "I" : "-", rate ? "R" : "-", static_cast<unsigned>(r.raw) & 0xff);
                else snprintf(row, sizeof row, " %s%s=%#x@%s", inner ? "I" : "-", rate ? "R" : "-", static_cast<unsigned>(r.raw), r.step.c_str());
                line += row;
            }
        table += line + "\n";
    }
    out("  shift (size): I = inner headers, R = rate fields; ok = accepted, else the low byte of OpenVideo's 0x819500xx");
    report += table;
    out("accepted layouts: %zu", accepted.size());
    for (const Accepted &a : accepted) {
        Try t;
        char label[96];
        snprintf(label, sizeof label, "start %+td inner=%d rate=%d", a.shift, a.inner, a.rate);
        t.label = label;
        t.rewrite = true;
        t.layout = layout_for(a.shift, a.inner, a.rate, 0, 4, 1, 0, 6000000, 6720000);
        const Result r = run(t);
        out("  %s: %s", label, r.opened() ? outcome(r).c_str() : ("Start " + outcome(r)).c_str());
        if (r.opened() && r.frames > 0 && !working.found) working = {true, a.shift, a.inner, a.rate};
        if (r.opened() && r.frames > 0 && a.shift == host_shift && a.inner == (host_shift >= 0) && a.rate == (host_shift >= 0))
            host_layout_starts = true;
    }
    if (working.found)
        out("working layout: shift %+td (total %#tx), inner headers %s, rate fields %s; the host uses shift %+td", working.shift,
            0x2e0 + working.shift, working.inner ? "yes" : "no", working.rate ? "yes" : "no", host_shift);
    else
        out("no layout starts: the rest uses the host's layout, and capture phases will fail the same way");
    if (!working.found) working = {false, host_shift, host_shift >= 0, host_shift >= 0};
}

// The words the validator checks: 180 in each free word of the working record, open only.
void phase_guards() {
    progress("guards");
    out("");
    out("== Words the validator guards (180 written into each free word of the working record, open only)");
    const ps_probe_layout base = working_layout();
    uint8_t record[PS_CAPTURE_RECORD_CAPACITY];
    const size_t size = ps_probe_build_record(record, sizeof record, &base);
    note("working record (%zu bytes): %s", size, hex(record, size).c_str());
    std::string refused, other;
    int tried = 0;
    for (size_t at = 0x30; at + 4 <= size; at += 4) {
        if (get32(record, at) || header_high_half(at, base)) continue;
        char label[40];
        snprintf(label, sizeof label, "guard %#zx", at);
        Try t = working_try(label);
        t.words = {{at, 180}};
        t.open_only = true;
        const Result r = run(t);
        ++tried;
        if (capture_taken) break;
        if (r.step == "open only") continue;
        char h[48];
        if (r.step == "OpenVideo" && static_cast<unsigned>(r.raw) == 0x81950001u) {
            snprintf(h, sizeof h, " %#zx", at);
            refused += h;
        } else {
            snprintf(h, sizeof h, " %#zx(%s %#x)", at, r.step.c_str(), static_cast<unsigned>(r.raw));
            other += h;
        }
    }
    out("tried %d words; refused by the validator (0x81950001):%s", tried, refused.empty() ? " none" : refused.c_str());
    if (!other.empty()) out("other answers:%s", other.c_str());
}

// What each free word of the encoder block changes: 2 written there, then bytes in 1 s with an IDR every 6 frames.
// On 10.20 this showed QP at 0xc4 (IDR) and 0xc8 (the rest).
void phase_values() {
    progress("values");
    out("");
    out("== Encoder block: 2 in each free word, bytes in 1 s against the record without it");
    Try base = working_try("values baseline");
    base.idr_every = 6;
    long baseline = 0;
    int good = 0;
    for (int i = 0; i < 2; i++) {
        const Result r = run(base);
        if (r.opened() && r.frames) {
            baseline += r.bytes;
            ++good;
        }
    }
    if (!good) {
        out("baseline doesn't start, skipped");
        return;
    }
    baseline /= good;
    out("baseline %ld bytes", baseline);
    const ps_probe_layout l = working_layout();
    uint8_t record[PS_CAPTURE_RECORD_CAPACITY];
    const size_t size = ps_probe_build_record(record, sizeof record, &l);
    const size_t end = std::min(size, static_cast<size_t>(0x198 + working.shift));
    std::string changed, failed;
    for (size_t at = 0xbc; at + 4 <= end; at += 4) {
        if (get32(record, at) || header_high_half(at, l)) continue;
        Try t = working_try("value 2 at " + std::to_string(at));
        t.words = {{at, 2}};
        t.idr_every = 6;
        const Result r = run(t);
        if (capture_taken) {
            out("  stopped: the capture stays busy");
            break;
        }
        char h[64];
        if (!r.opened() || !r.frames) {
            snprintf(h, sizeof h, " %#zx(%s %#x)", at, r.opened() ? "no frames" : r.step.c_str(), static_cast<unsigned>(r.raw));
            failed += h;
        } else if (r.bytes > baseline * 3 || r.bytes * 3 < baseline) {
            snprintf(h, sizeof h, " %#zx(x%.1f)", at, static_cast<double>(r.bytes) / static_cast<double>(baseline));
            changed += h;
        }
    }
    out("words that change the bytes 3x or more:%s", changed.empty() ? " none" : changed.c_str());
    out("words that stop the capture:%s", failed.empty() ? " none" : failed.c_str());
}

void phase_rate() {
    progress("rate");
    out("");
    out("== Rate and QP");
    if (working.rate) {
        for (const uint32_t average : {3000000u, 20000000u}) {
            Try t = working_try("rate fields " + std::to_string(average / 1000) + " kbit/s");
            t.layout.average = average;
            t.layout.peak = average / 100 * 112;
            t.idr_every = 6;
            const Result r = run(t);
            out("  rate fields at %u kbit/s: %s, %ld kbit/s delivered", average / 1000, outcome(r).c_str(), r.kbit(1000));
        }
    } else {
        Try t = working_try("rate fields as on 13.00");
        t.layout.rate_fields = 1;
        const Result r = run(t);
        out("13.00's rate fields on the working layout: %s", r.opened() ? outcome(r).c_str() : ("fail at " + outcome(r)).c_str());
    }
    const std::pair<uint32_t, uint32_t> pairs[] = {{0, 0}, {36, 40}, {28, 32}, {20, 24}, {52, 0}};
    for (const auto &q : pairs) {
        Try t = working_try("QP " + std::to_string(q.first) + "/" + std::to_string(q.second));
        if (q.first) t.words.push_back({ps_qp_offset(working.shift), q.first});
        if (q.second) t.words.push_back({ps_qp_offset(working.shift) + 4, q.second});
        t.idr_every = 6;
        const Result r = run(t);
        if (!q.first && r.opened()) found.qp_none = r.bytes;
        if (q.first == 20 && r.opened()) found.qp_fine = r.bytes;
        if (q.first == 52) found.qp_52_fails = !r.opened();
        out("  QP at 0xc4/0xc8 %u/%u: %s, %ld kbit/s, largest frame %d", q.first, q.second, r.opened() ? outcome(r).c_str() : ("fail at " + outcome(r)).c_str(),
            r.kbit(1000), r.largest);
    }
    out("  (on 10.20: bytes grow as the QP falls, and 52 fails Start with 0x80c78000: 0xc4/0xc8 are QP)");
    // A client can lower or raise its bitrate during a stream. On 13.00 raising it above the opening rate kills the channel;
    // on 10.20 the same rate went through and 20 Mbit/s killed it (0x80c78000 on the next read).
    for (const uint32_t to : {0u, 20000000u}) {
        Try b = working_try(to ? "SetVideoBitrate to 20000 kbit/s" : "SetVideoBitrate at the opening rate");
        b.read_ms = 1500;
        b.bitrate_at_ms = 500;
        b.bitrate_to = to;
        const Result r = run(b);
        if (r.opened() && to) found.bitrate_breaks = r.read_rc != 0;
        out("SetVideoBitrate %s, 0.5 s in: %s, call rc=%d, %d frames after it%s", to ? "to 20000 kbit/s (opened at 6000)" : "at the opening rate",
            r.opened() ? "opened" : "open failed", r.bitrate_rc, r.frames_after_bitrate, r.read_rc ? ", then the read failed" : "");
        if (r.read_rc) out("  read error raw %#x (0x80c78000: the channel is gone)", static_cast<unsigned>(r.read_raw));
    }
}

// Only when firmware_probe.phases names it: for a firmware where neither 13.00's rate fields nor 10.x's QP do anything (9.60). An
// IDR every 6 frames makes the bytes depend on the encoder's settings even on a quiet screen. A word counts when bytes or the
// largest frame move by 1.4 times against the baseline.
void phase_qp_steps() {
    progress("qp");
    out("");
    out("== QP pairs at %#zx, bytes in 1 s with an IDR every 6 frames", ps_qp_offset(working.shift));
    const std::pair<uint32_t, uint32_t> pairs[] = {{0, 0}, {36, 40}, {30, 34}, {27, 31}, {24, 28}, {21, 25}, {18, 22}, {15, 19}, {12, 16}, {10, 14}, {8, 12}};
    for (const auto &q : pairs) {
        Try t = working_try("QP step " + std::to_string(q.first));
        if (q.first) t.words = {{ps_qp_offset(working.shift), q.first}, {ps_qp_offset(working.shift) + 4, q.second}};
        t.idr_every = 6;
        const Result r = run(t);
        out("  %u/%u: %s, %ld kbit/s, largest frame %d", q.first, q.second, r.opened() ? outcome(r).c_str() : ("fail at " + outcome(r)).c_str(), r.kbit(1000), r.largest);
    }
}

void phase_rate_search() {
    progress("ratesearch");
    out("");
    out("== Rate search: bytes in 1 s with an IDR every 6 frames");
    long baseline = 0;
    int largest = 0;
    const auto measure = [](const std::string &label, const Words &words) {
        Try t = working_try(label);
        t.words = words;
        t.idr_every = 6;
        return run(t);
    };
    for (int i = 0; i < 2; i++) {
        const Result r = measure("search baseline", {});
        baseline = r.bytes;
        largest = r.largest;
    }
    out("baseline %ld bytes, largest frame %d", baseline, largest);
    if (baseline <= 0) return;
    const auto moved = [&](const Result &r) {
        return r.bytes * 10 > baseline * 14 || r.bytes * 14 < baseline * 10 || r.largest * 10 > largest * 14 || r.largest * 14 < largest * 10;
    };
    const auto report_word = [&](const char *what, const Result &r, std::string &changed, std::string &failed) {
        char h[96];
        if (!r.opened() || !r.frames) {
            snprintf(h, sizeof h, " %s(%s %#x)", what, r.opened() ? "no frames" : r.step.c_str(), static_cast<unsigned>(r.raw));
            failed += h;
        } else if (moved(r)) {
            snprintf(h, sizeof h, " %s(%ld bytes, largest %d)", what, r.bytes, r.largest);
            changed += h;
        }
    };
    const ps_probe_layout l = working_layout();
    uint8_t record[PS_CAPTURE_RECORD_CAPACITY];
    const size_t size = ps_probe_build_record(record, sizeof record, &l);
    const size_t end = std::min(size, static_cast<size_t>(0x198 + working.shift));
    for (const uint32_t value : {40000000u, 20u}) {
        std::string changed, failed;
        for (size_t at = 0x30; at + 4 <= end; at += 4) {
            if (get32(record, at) || header_high_half(at, l)) continue;
            char name[24];
            snprintf(name, sizeof name, "%#zx", at);
            const Result r = measure("search " + std::to_string(value) + " at " + name, {{at, value}});
            if (capture_taken) {
                out("  stopped: the capture stays busy");
                return;
            }
            report_word(name, r, changed, failed);
        }
        out("%u in each free word: moved:%s; stopped:%s", value, changed.empty() ? " none" : changed.c_str(), failed.empty() ? " none" : failed.c_str());
    }
    std::string changed, failed;
    for (const uint32_t mode : {1u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 16u}) {
        char name[24];
        snprintf(name, sizeof name, "0xc0=%u", mode);
        report_word(name, measure(std::string("search ") + name, {{0xc0, mode}}), changed, failed);
    }
    out("0xc0 as a mode: moved:%s; stopped:%s", changed.empty() ? " none" : changed.c_str(), failed.empty() ? " none" : failed.c_str());
    changed.clear();
    failed.clear();
    constexpr uint32_t average = 40000000, peak = 44000000, per_frame = static_cast<uint32_t>(uint64_t{peak} * 1001 / 60000);
    for (const size_t at : {size_t{0xc4}, size_t{0xc8}, size_t{0xcc}, size_t{0xd0}, size_t{0xd4}, size_t{0xd8}, size_t{0xe0}, size_t{0xe8}}) {
        char name[24];
        snprintf(name, sizeof name, "%#zx", at);
        report_word(name, measure(std::string("search rate fields at ") + name, {{at, average}, {at + 4, peak}, {at + 8, per_frame}}), changed, failed);
    }
    out("40 Mbit/s as average, peak and bits per frame from: moved:%s; stopped:%s", changed.empty() ? " none" : changed.c_str(),
        failed.empty() ? " none" : failed.c_str());
    for (const uint32_t to : {1000000u, 3000000u, 6000000u, 8000000u, 10000000u, 12000000u, 16000000u}) {
        Try b = working_try("search SetVideoBitrate " + std::to_string(to / 1000));
        b.read_ms = 1500;
        b.bitrate_at_ms = 300;
        b.bitrate_to = to;
        b.idr_every = 6;
        const Result r = run(b);
        out("SetVideoBitrate to %u kbit/s, 0.3 s in: rc=%d, %d frames after it, %ld kbit/s over 1.5 s, largest %d%s", to / 1000, r.bitrate_rc,
            r.frames_after_bitrate, r.kbit(1500), r.largest, r.read_rc ? ", then the read failed" : "");
    }
}

void phase_codecs() {
    progress("codecs");
    out("");
    out("== Codecs on the working layout");
    struct Case {
        const char *name;
        uint32_t codec, ten_bit;
        bool hevc_fields, full;
        const char *save;
    };
    const Case cases[] = {
        {"H.264 10-bit (13.00: 0x80c70004)", 0, 1, false, false, nullptr},
        {"HEVC 8-bit, cfg+0x24 alone", 1, 0, false, false, nullptr},
        {"HEVC 8-bit, 13.00's Remote Play fields", 1, 0, true, false, "hevc8_remoteplay_fields.h265"},
        {"HEVC 8-bit, those fields moved with the shift", 1, 0, true, true, nullptr},
        {"HEVC 10-bit, cfg+0x24 alone", 1, 1, false, false, nullptr},
    };
    for (const Case &c : cases) {
        Try t;
        t.label = c.name;
        t.rewrite = true;
        t.codec = c.codec;
        t.ten_bit = c.ten_bit;
        t.peak = 6600000;
        t.layout = working_layout(c.codec, 4, 1, c.ten_bit);
        t.layout.hevc_fields = c.hevc_fields;
        t.layout.hevc_high = c.full ? working.shift : 0;
        t.read_ms = 1500;
        t.save = c.save;
        const Result r = run(t);
        if (c.codec == 1 && !c.ten_bit && !c.hevc_fields) found.hevc_minimal = r.opened() && r.frames > 0;
        out("  %s: %s", c.name, r.opened() ? outcome(r).c_str() : ("fail at " + outcome(r)).c_str());
    }
}

void phase_modes() {
    progress("modes");
    out("");
    out("== Modes on the working layout (resource 182 from 1440p, HEVC purpose 6 from 1440p, as the host does)");
    const std::pair<uint32_t, uint32_t> modes[] = {{1, 1}, {2, 1}, {3, 1}, {4, 0}, {5, 1}, {6, 1}};
    const char *names[] = {"360p60", "540p60", "720p60", "1080p30", "1440p60", "2160p60"};
    for (uint32_t codec = 0; codec < 2; codec++)
        for (size_t i = 0; i < 6; i++) {
            Try t;
            t.label = std::string(codec ? "HEVC " : "H.264 ") + names[i];
            t.rewrite = true;
            t.codec = codec;
            t.resolution = modes[i].first;
            t.timing = modes[i].second;
            t.peak = codec ? 6600000 : 6720000;
            t.layout = working_layout(codec, modes[i].first, modes[i].second, 0);
            t.attempts = 2;
            const Result r = run(t);
            if (r.opened() && r.frames) found.modes_ok += " " + t.label;
            out("  %s: %s", t.label.c_str(), r.opened() ? outcome(r).c_str() : ("fail at " + outcome(r)).c_str());
        }
}

void phase_purposes() {
    progress("purposes");
    out("");
    out("== Purpose (cfg+0x14) and resource (cfg+0x28 and the arbitrator's id), H.264 1080p60");
    std::string row = "  purpose:";
    for (uint32_t purpose = 0; purpose < 8; purpose++) {
        Try t = working_try("purpose " + std::to_string(purpose));
        t.layout.purpose = purpose;
        t.read_ms = 400;
        row += " " + std::to_string(purpose) + "=" + cell(run(t));
    }
    report += row + "\n";
    row = "  resource:";
    for (const int id : {180, 177, 185, 175, 182}) {
        Try t = working_try("resource " + std::to_string(id));
        t.layout.resource = static_cast<uint32_t>(id);
        t.resource = id;
        t.read_ms = 400;
        row += " " + std::to_string(id) + "=" + cell(run(t));
    }
    report += row + "\n";
}

const char *yes_no(int v) { return v < 0 ? "not tried" : v ? "yes" : "no"; }

void summary() {
    out("");
    out("== Summary: what the host needs on this firmware");
    out("host's own code: H.264 %s, HEVC 8-bit %s, HEVC 10-bit %s", yes_no(found.host_h264), yes_no(found.host_hevc8), yes_no(found.host_hevc10));
    if (working.found) {
        if (!host_layout_starts &&
            (working.shift != host_shift || working.inner != (host_shift >= 0) || working.rate != (host_shift >= 0)))
            out("record: CHANGE record_shift() to %+td (total %#tx), inner headers %s, rate fields %s (the host has %+td)", working.shift,
                0x2e0 + working.shift, working.inner ? "yes" : "no", working.rate ? "yes" : "no", host_shift);
        else
            out("record: the host's layout is right (shift %+td)", host_shift);
    } else {
        out(layout_tried ? "record: no layout started; see the layout table and capture.log for the step that failed" : "record: layout phase not run");
    }
    if (found.qp_none && found.qp_fine)
        out("QP at 0xc4/0xc8: %s (QP 20/24 gave %.1fx the bytes of none; QP 52 fails Start: %s)",
            found.qp_fine > found.qp_none * 2 ? "yes, as on 10.20" : "no clear effect", static_cast<double>(found.qp_fine) / static_cast<double>(found.qp_none),
            yes_no(found.qp_52_fails));
    out("SetVideoBitrate above the opening rate breaks the channel: %s", yes_no(found.bitrate_breaks));
    out("HEVC with cfg+0x24 alone: %s; H.264 slices per 1080p frame: %d%s", yes_no(found.hevc_minimal), found.host_slices,
        found.host_slices > 32 ? " (over FFmpeg's 32: Windows needs HEVC or software decoding)" : "");
    out("modes that deliver frames:%s", found.modes_ok.empty() ? " none tried" : found.modes_ok.c_str());
    if (!lingering.empty()) {
        out("the resource stayed listed after these attempts (the next open waited):");
        report += lingering;
    }
    if (capture_taken) out("the capture stayed busy for good: some phases were skipped; check that nothing streams and run it again");
}

}  // namespace

int main() {
    mkdir("/data/prosperosunshine", 0777);
    mkdir(kDir, 0777);
    detail = fopen((std::string(kDir) + "/detail.log").c_str(), "w");
    {
        std::ifstream in("/data/prosperosunshine/firmware_probe.phases");
        std::getline(in, phases_wanted);
    }
    klog_printf("ProsperoSunshine: firmware_probe start\n");
    out("ProsperoSunshine firmware probe (docs/firmware-probe.md)%s%s", phases_wanted.empty() ? "" : ", phases: ", phases_wanted.c_str());
    // Never during a stream: the probe stops the host, and with it the user's stream. Asked again right before that.
    const auto stream_running = [] {
        before("host: STATUS");
        std::string why;
        if (!host_control::busy(why)) return false;
        out("stopped here: %s; %s, and run the probe again", why.c_str(),
            why == "the host is streaming" ? "end the stream" : "if nothing streams, restart the host from the app");
        write_file("report.txt", report);
        write_file("progress.txt", "done (a stream was running, or the host didn't answer)\n");
        if (detail) fclose(detail);
        return true;
    };
    if (stream_running()) return 1;
    // Samples only when the user asked for them, and for this run alone; an earlier run's go either way.
    keep_samples = std::ifstream(kSamplesOptIn).good();
    unlink(kSamplesOptIn);
    for (const char *name : {"h264_1080p60.h264", "hevc8_1080p60.h265", "hevc10_1080p60.h265", "hevc8_remoteplay_fields.h265",
                             "audio_pcm_f32le_2ch.raw", "audio_opus.bin"}) {
        unlink((std::string(kSamples) + "/" + name).c_str());
        unlink((std::string(kDir) + "/" + name).c_str());  // where the probe's version 1 saved them
    }
    if (keep_samples) mkdir(kSamples, 0777);
    else rmdir(kSamples);
    out("samples: %s", keep_samples ? "video and audio of the run in firmware_probe/samples/ (asked for with firmware_probe.samples). "
                                      "They show the screen and may show the user's name: keep them out of public issues, and "
                                      "send them only to a developer who asks for them"
                                    : "none (an empty file /data/prosperosunshine/firmware_probe.samples asks for them, for one "
                                      "run): every file of this run is text");
    char header[128];
    snprintf(header, sizeof header, "ProsperoSunshine: firmware_probe capture steps, firmware=%#x", kernel_get_fw_version());
    ps_diag_open((std::string(kDir) + "/capture.log").c_str(), header);

    phase_environment();
    host_shift = ps_capture_record_shift();  // also when the host phase is skipped
    // The capture is one process's at a time, and a running host holds it while it streams: stop it over its control port.
    if (stream_running()) return 1;
    const bool host_was_running = host_control::running();
    std::string host_detail;
    if (host_was_running) before("host: QUIT");
    const bool stopped = host_control::stop(host_detail);
    out("host: %s", host_detail.c_str());
    if (!stopped) {
        out("stopped here: the host didn't quit; stop it in the app and run the probe again");
        write_file("report.txt", report);
        write_file("progress.txt", "done (host still running)\n");
        return 1;
    }
    ps_capture_set_record_hook(record_hook, nullptr);

    if (wanted("host")) phase_host();
    if (wanted("symbols")) phase_symbols();
    if (wanted("layout")) phase_layout();
    else working = {false, host_shift, host_shift >= 0, host_shift >= 0};
    if (wanted("guards")) phase_guards();
    if (wanted("values")) phase_values();
    if (wanted("rate")) phase_rate();
    if (named("ratesearch")) phase_rate_search();
    if (named("qp")) phase_qp_steps();
    if (wanted("codecs")) phase_codecs();
    if (wanted("modes")) phase_modes();
    if (wanted("purposes")) phase_purposes();

    ps_capture_set_record_hook(nullptr, nullptr);
    ps_capture_set_open_only(0);
    ps_capture_set_open_attempts(0);
    summary();
    out("");
    const std::string title_at_end = title_in_front();
    out("in front at the end: %s, %s (a title that starts or ends during the run makes captures fail with 0x81950009)",
        in_front(title_at_end), title_at_end == title_at_start ? "the same as at the start" : "NOT the same as at the start");
    // Start the host again if one ran, or if host_restart.next names one to start.
    const std::string next = host_control::next_elf();
    std::ifstream asked(host_control::kNextFile);
    if (next.empty()) {
        out("host: not started again (host_restart.next says none)");
    } else if (host_was_running || asked.good()) {
        before("host: start %s", next.c_str());
        const bool up = host_control::start(next, host_detail);
        out("host: %s%s", host_detail.c_str(), up ? "" : "; start it in the app");
    } else {
        out("host: none was running, none started");
    }
    out("done in %ld s", elapsed_ms() / 1000);
    write_file("report.txt", report);
    write_file("progress.txt", "done\n");
    klog_printf("ProsperoSunshine: firmware_probe done\n");
    if (detail) fclose(detail);
    return 0;
}
