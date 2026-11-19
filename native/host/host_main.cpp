// The native game's host: loads the game module (smg_game.dll), gives it
// Nebula's services (nebula_game_api.h) and runs it. Built three ways:
//   - nebula_native_host (native/CMakeLists.txt): headless. Records are dropped as they arrive; audio is paced but not
//     played; no remotes connect.
//   - Nebula (the top-level build, NEBULA_HOST_RENDERER): the records go to Nebula's D3D12 renderer.
//   - On SDL platforms (platform/sdl, NEBULA_HOST_SDL): the same with the Vulkan renderer, SDL's window, sound and
//     input; on Android SDL's activity calls SDL_main.
//
//   <host> --pak <game.pak> --saves <dir> [--module <smg_game.dll>] [--seconds N] (docs/using-nebula/command-line-arguments.md)
//   Windowed hosts also take --host <port> or --join <address[:port]> (online co-op).
#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include "galaxy/windows_paths.h"
#include "galaxy/windows_child.h"
#include "run_sibling_win32.h"
#endif
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
#include <dwmapi.h>
#endif
#include "galaxy/frame_phase.h"
#include "galaxy/profiled_threads.h"
#include "galaxy/render/present_clock.h"
#include <nebula_cooked_texture.h>
#include <nebula_fingerprint.h>
#include <nebula_game_api.h>

#include "galaxy/atomic_file.h"
#include "galaxy/coop_chat.h"
#include "galaxy/coop_menu.h"
#include "galaxy/coop_nebulanet.h"
#include "galaxy/coop_mods.h"
#include "galaxy/coop_popups.h"
#include "galaxy/coop_saves.h"
#include "galaxy/coop_session.h"
#include "galaxy/coop_snapshot.h"
#include "galaxy/coop_teleport.h"
#include "galaxy/coop_world.h"
#include "galaxy/luma_assets_service.h"
#include "galaxy/luma_audio_service.h"
#include "galaxy/launch_args.h"
#include "galaxy/host_extension.h"
#include "galaxy/path_text.h"
#include "galaxy/platform_paths.h"
#include "galaxy/game_step.h"
#include "galaxy/asset_store.h"
#include "galaxy/save_files.h"
#include "galaxy/crash_report.h"
#include "galaxy/game_module.h"
#include "galaxy/luma_guard_host.h"
#if defined(_WIN32)
#include "crash_watch.h"
#endif
#if (defined(NEBULA_HOST_RENDERER) && defined(_WIN32)) || (defined(NEBULA_HOST_SDL) && !defined(__ANDROID__) && !defined(NEBULA_HOST_APPLE))
#include "galaxy/hidden_start.h"
#endif
#include "galaxy/desktop_window.h"
#include "galaxy/discord_presence.h"
#include "galaxy/mii_formats.h"
#include "galaxy/mii_import.h"
#include "galaxy/luma_input_service.h"
#include "galaxy/luma_mods_service.h"
#include "galaxy/mods.h"
#include "galaxy/game_instance.h"
#include <luma_input.h>
#include <luma_refs.h>
#include "galaxy/render/host_window.h"
#include "galaxy/render/os_hdr.h"
#include "galaxy/mod_log.h"
#include "galaxy/mod_screens.h"
#include "galaxy/mod_start_check.h"
#include "galaxy/mod_plugins.h"
#include "galaxy/render/mod_post.h"
#include "galaxy/render/mod_shaders.h"
#include "galaxy/render/program_table.h"
#include "galaxy/pointer_control.h"
#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/display_aspect.h"
#include "galaxy/benchmark.h"
#include "galaxy/calibration_scene.h"
#include "galaxy/ui/benchmark_menu.h"
#include "galaxy/ui/hdr_calibration.h"
#include "galaxy/ui/settings_benchmark_page.h"
#if defined(NEBULA_DIAG_REPORTS)
#include "galaxy/diag_report.h"
#endif
#include "galaxy/frame_timeline.h"
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
#include "galaxy/controller_hid.h"
#endif
#include "galaxy/render_api.h"
#include "galaxy/native_audio.h"
#include "galaxy/game_paths.h"
#include "galaxy/native_input.h"
#include "galaxy/loss_report.h"
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
#include "galaxy/render/host_window_win32.h"
#include "galaxy/render/renderer_check.h"
#endif
#include "galaxy/perf_hint.h"
#include "galaxy/power_policy.h"
#if defined(_WIN32)
#include "galaxy/windows_power.h"
#endif
#include "galaxy/platform_time.h"
#include "galaxy/precise_wait.h"
#include "galaxy/input/controls_engine.h"
#include "galaxy/input/wiimote_motion.h"
#include "galaxy/achievements.h"
#include "galaxy/stats.h"
#if (defined(_WIN32) && !defined(NEBULA_HOST_SDL)) || defined(__ANDROID__)
// A real Wii Remote (wiimote_hid.h): over Windows HID, or on a DolphinBar through Android's USB host.
#define NEBULA_REAL_WIIMOTE 1
#include "galaxy/input/wiimote_input.h"
#include "galaxy/input/wiimote_motion_pointer.h"
#include "galaxy/wiimote_hid.h"
#endif
#include "galaxy/runtime_settings.h"
#include "galaxy/render/galaxy_select.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/texture_store.h"
#if defined(NEBULA_HOST_RENDERER)
#include "render/core/frame_core.h"
#include "galaxy/ui/launcher.h"
#include "galaxy/ui/launcher_model.h"
#if defined(NEBULA_UPDATE_CHECK)
#include "galaxy/update.h"
#endif
#endif
#include "galaxy/save_transfer.h"
#include <nebula_frame_list.h>
#include "galaxy/render/rt_materials.h"
#include "galaxy/render/rt_caps.h"
#include "galaxy/render/texture_pack.h"
#if defined(NEBULA_HOST_D3D12)
#endif

#if defined(_WIN32)
#include <crtdbg.h>
#else
#include "galaxy/virtual_keys.h"
#endif
#endif
#if !defined(NEBULA_HOST_RENDERER)
namespace galaxy::host {
// The headless host gives the platform no performance hints.
inline void perf_hint_add_current_thread() noexcept {}
inline void perf_hint_set_target(int64_t) noexcept {}
inline int64_t perf_hint_frame_done() noexcept { return 0; }
inline bool perf_hint_on_battery() noexcept { return false; }
}  // namespace galaxy::host
#endif
#if defined(NEBULA_HOST_SDL)
#include "sdl_host.h"

// The host of platform/apple gives the same services without SDL.
#if !defined(NEBULA_HOST_NATIVE_APPLE)
#include <SDL3/SDL_main.h>
#endif
#endif
#if defined(NEBULA_HOST_APPLE)
#include "apple_host.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <ctime>
#include <span>
#if defined(_WIN32)
#include <io.h>
#endif
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <shellapi.h>
#include <intrin.h>
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>
#include "cpu_level_x86.h"
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__ANDROID__)
#include <malloc.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#endif
#include <sys/wait.h>
#endif
#include "galaxy/platform_paths.h"

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

// Sleeps until `when` on a high-resolution waitable timer (Windows 10 1803+; the scheduler tick otherwise), so the
// frame, the audio interface and the remotes keep their periods to well under a millisecond.
void sleep_until_precise(Clock::time_point when) {
#if defined(_WIN32)
    thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                       TIMER_ALL_ACCESS);
    const auto now = Clock::now();
    if (when <= now) {
        return;
    }
    if (timer == nullptr) {
        std::this_thread::sleep_until(when);
        return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>((std::chrono::duration_cast<std::chrono::nanoseconds>(when - now).count() + 99) / 100);
    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer, INFINITE);
#else
    std::this_thread::sleep_until(when);
#endif
}
const Clock::time_point kStart = Clock::now();

std::string utf8_of(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

void log_line(const char* tag, const char* text) {
    const double t = std::chrono::duration<double>(Clock::now() - kStart).count();
    std::fprintf(stdout, "[%9.3f] %s %s\n", t, tag, text);
    std::fflush(stdout);
    galaxy::extensions::log_line(tag, text);
}

#if !defined(_WIN32)
// A crash report line from a fault handler (luma_guard_host.h set_crash_line): log_line's form with write only.
void crash_line_from_signal(const char* tag, const char* text) noexcept {
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - kStart).count();
    char line[640];
    std::size_t at = 0;
    const auto add = [&](char c) {
        if (at + 1 < sizeof(line)) line[at++] = c;
    };
    char digits[24];
    int n = 0;
    for (long long s = ms / 1000; n == 0 || s != 0; s /= 10) digits[n++] = static_cast<char>('0' + s % 10);
    add('[');
    for (int pad = n; pad < 5; ++pad) add(' ');
    while (n > 0) add(digits[--n]);
    add('.');
    add(static_cast<char>('0' + ms / 100 % 10));
    add(static_cast<char>('0' + ms / 10 % 10));
    add(static_cast<char>('0' + ms % 10));
    add(']');
    for (const char* parts[] = {" ", tag, " ", text}; const char* part : parts) {
        for (; part != nullptr && *part != '\0'; ++part) add(*part);
    }
    add('\n');
    (void)!write(1, line, at);
}
#endif

// The start, step by step ("boot:" lines, milliseconds from the process's creation): the black screen before the
// title is their sum.
void boot_mark(const char* step) {
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - kStart).count();
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{}, now{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        GetSystemTimePreciseAsFileTime(&now);
        const auto ticks = [](const FILETIME& f) {
            return static_cast<double>((static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime);
        };
        ms = (ticks(now) - ticks(created)) / 1.0e4;
    }
#endif
    char text[128];
    std::snprintf(text, sizeof(text), "%s at %.0f ms", step, ms);
    log_line("boot:", text);
}

// Crash reports (galaxy/crash_report.h): why Nebula stopped, as a code with what to do. What the failing thread knew,
// kept for the report (gathered on that thread: its activity is its own).
galaxy::crash::Facts gCrashFacts;
char gCrashText[1024];
char gCrashActivityFile[512];
char gCrashModRead[640];
char gCrashPlugin[128];

// While mods' code runs: the file a crash leaves, so that the next start leaves the code off once.
std::filesystem::path gCodeCrashFile;

void gather_crash_facts(galaxy::crash::Facts::Kind kind, const char* text, bool in_game = false,
                        bool stack_overflow = false) noexcept {
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    galaxy::host::stop_all_rumble();
#endif
    if (!gCodeCrashFile.empty()) {
        std::filesystem::path file;
        file.swap(gCodeCrashFile);
#if defined(_WIN32)
        if (FILE* out = _wfopen(file.c_str(), L"wb")) {
            std::fclose(out);
        }
#else
        if (FILE* out = std::fopen(file.c_str(), "wb")) {
            std::fclose(out);
        }
#endif
    }
    std::snprintf(gCrashText, sizeof(gCrashText), "%s", text != nullptr ? text : "");
    gCrashFacts.kind = kind;
    gCrashFacts.in_game = in_game;
    gCrashFacts.stack_overflow = stack_overflow;
    gCrashFacts.text = gCrashText;
    gCrashFacts.activity = galaxy::crash::current_activity(gCrashActivityFile, sizeof(gCrashActivityFile));
    gCrashFacts.activity_file = gCrashActivityFile;
    double seconds = -1.0;
    if (galaxy::crash::last_mod_read(gCrashModRead, sizeof(gCrashModRead), &seconds)) {
        gCrashFacts.last_mod_read = gCrashModRead;
        gCrashFacts.last_mod_seconds = seconds;
    }
    gCrashFacts.mods_change_game = galaxy::crash::session_mods_change_game();
}

// Crash dumps and reports: --crash-dumps, else crashes\ in Nebula's data folder for the player's executable and crashes\
// in the working directory for the headless host.
fs::path crash_folder() {
    if (!::galaxy::launch_args().crash_dumps.empty()) return ::galaxy::launch_args().crash_dumps;
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
    if (const fs::path data = ::galaxy::nebula_data_directory(); !data.empty()) return data / "crashes";
#endif
    return fs::path("crashes");
}

// The player sees the report; a hidden start (a test) and the headless host keep it in its file.
bool crash_dialog_wanted() {
#if defined(NEBULA_HOST_RENDERER) || defined(NEBULA_HOST_SDL)
    return !::galaxy::launch_args().started_hidden;
#else
    return false;
#endif
}

FILE* open_for_writing(const fs::path& file) {
#if defined(_WIN32)
    return _wfopen(file.c_str(), L"wb");
#else
    return std::fopen(file.c_str(), "wb");
#endif
}

#if defined(NEBULA_HOST_RENDERER)
// The unlocks and counts waiting out their write delay (galaxy::achievements 0.25 s, galaxy::stats 1.5 s) are written before a
// process ends the hard way (a crash, a fatal error). On a thread of its own and waited for at most `wait_ms`: the thread that
// failed may hold the lock of one of them, and the report must not wait on it for ever.
#if defined(_WIN32)
DWORD WINAPI flush_progress_thread(LPVOID) {
    galaxy::achievements::flush();
    galaxy::stats::flush();
    return 0;
}
#endif
void flush_progress_bounded(unsigned wait_ms) {
#if defined(_WIN32)
    if (const HANDLE thread = CreateThread(nullptr, 0, flush_progress_thread, nullptr, 0, nullptr); thread != nullptr) {
        WaitForSingleObject(thread, wait_ms);
        CloseHandle(thread);
    }
#else
    (void)wait_ms;
    galaxy::achievements::flush();
    galaxy::stats::flush();
#endif
}
#endif

// gCrashFacts as a report: its code logged, its text written beside `dump` (or as nebula-<time>.txt in the crash
// folder) and, for the player, shown by a new instance of this executable (this process may be what failed; it ends
// right after). The SDL hosts show it in a message box here.
void report_crash_facts(const fs::path& dump) {
    const galaxy::crash::Report report = galaxy::crash::classify(gCrashFacts);
    char line[160];
    std::snprintf(line, sizeof(line), "%s %s", report.code, report.title);
    log_line("crash:", line);
    char technical[1600];
    std::snprintf(technical, sizeof(technical), "%s%s%s%s%s", gCrashText,
                  gCrashFacts.last_mod_read.empty() ? "" : "\nLast file read from a mod: ", gCrashModRead,
                  dump.empty() ? "" : "\nDump: ", dump.empty() ? "" : utf8_of(dump.filename()).c_str());
    static char text[8192];
    const std::size_t length = galaxy::crash::format_report(report, technical, text, sizeof(text));
    std::error_code error;
    fs::path file = dump;
    if (file.empty()) {
        const fs::path folder = crash_folder();
        fs::create_directories(folder, error);
        const auto stamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char name[64];
        std::strftime(name, sizeof(name), "nebula-%Y%m%d-%H%M%S.txt", std::localtime(&stamp));
        file = folder / name;
    } else {
        file.replace_extension(".txt");
    }
    if (FILE* out = open_for_writing(file)) {
        std::fwrite(text, 1, length, out);
        std::fclose(out);
        std::snprintf(line, sizeof(line), "report %s", utf8_of(file.filename()).c_str());
        log_line("crash:", line);
    }
    if (!crash_dialog_wanted()) return;
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
    const std::wstring exe = ::galaxy::windows::executable_path();
    std::wstring command = L"\"" + exe + L"\" --crash-report \"" + file.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!exe.empty() && CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#elif defined(NEBULA_HOST_SDL)
    const std::string_view shown(text, length);
    const std::size_t technical_at = shown.find("\nTechnical details:");
    const std::string message(shown.substr(0, technical_at));
    galaxy::sdl::show_fatal(message.c_str());
#endif
}

[[noreturn]] void die(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    log_line("fatal:", text);
    std::fflush(stdout);
    gather_crash_facts(galaxy::crash::Facts::Kind::Startup, text);
    report_crash_facts({});
#if defined(NEBULA_HOST_RENDERER)
    flush_progress_bounded(1500u);
#endif
    std::fflush(stdout);
    std::_Exit(3);
}

// The module's entry points.
struct Module {
    decltype(&nebula_game_api_fingerprint) api_fingerprint;  // null in a module from before fingerprints
    decltype(&nebula_game_attach) attach;
    decltype(&nebula_game_run) run;
    void* module;
    decltype(&nebula_game_sound_frame) audio_tick;
    decltype(&nebula_game_mii_valid) mii_valid;
    decltype(&nebula_game_mii_changed) mii_changed;
};
Module gModule;

// Progress counters for the host's log.
std::atomic<uint32_t> gFileReads{0};
std::atomic<uint32_t> gSaveOps{0};
std::atomic<uint32_t> gSoundBlocks{0};
std::atomic<uint32_t> gSoundSubframes{0};
std::atomic<uint32_t> gSceneBegins{0};
// The game frame running (frame_begun): it numbers the frame lists the game submits.
std::atomic<uint32_t> gGameFrames{0};

void* find_symbol(void* module, const char* name) {
#if defined(_WIN32)
    void* p = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
#else
    void* p = dlsym(module, name);
#endif
    if (p == nullptr) {
        die("the game module has no %s", name);
    }
    return p;
}

#if defined(_WIN32)
// --diagnostics profile: the game thread (every game thread runs on it) and the threads that add themselves
// (galaxy/profiled_threads.h: the render thread) sampled each millisecond from their start to the run's end (at most 10
// minutes), with their callers, each into the diagnostics folder's <name>-profile-<pid>.txt at exit (the game's
// game-profile-<pid>.txt): the modules loaded ("module <base> <size> <name>"), then a line a sample ("sample t<QPC>" and
// the return addresses, the innermost first; the QPC is the timeline's clock, so a stretch of the timeline picks its
// samples). tools/perf/game_profile.py names the game module's frames from its link map.
namespace game_profile {

constexpr int kDepth = 24;
using Frames = std::array<DWORD64, kDepth>;

// The top of a sampled thread's stack, copied while it is suspended (StackCopy::take) and walked after it resumes.
constexpr std::size_t kStackCopyBytes = 64u * 1024u;
struct StackCopy {
    alignas(16) std::array<std::byte, kStackCopyBytes> bytes;
    std::size_t size = 0u;
};

// A sample's stack from `context`, whose stack pointer (and every value that pointed into the copied range) points
// into `copy`: the walk stops where the copy ends.
int walk(CONTEXT context, const StackCopy& copy, Frames& frames) noexcept {
    int depth = 0;
    const auto low = reinterpret_cast<DWORD64>(copy.bytes.data());
    const DWORD64 high = low + copy.size;
    __try {
        while (depth < kDepth && context.Rip != 0 && context.Rsp >= low && context.Rsp + 8u <= high) {
            frames[depth++] = context.Rip;
            DWORD64 image = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
            if (function == nullptr) {
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
            } else {
                void* handler = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler, &establisher,
                                 nullptr);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return depth;
}

constexpr std::size_t kMostSamples = 600'000u;  // a thread's: 10 minutes, 115 MB
std::thread g_sampler;
std::atomic<bool> g_stop{false};

struct Sample {
    std::int64_t qpc;
    int depth;
    Frames frames;
};

struct Target {
    HANDLE thread = nullptr;
    const char* name = "";
    std::vector<Sample> samples;
};

// One sample of `target`. The thread is suspended only while its registers and the top of its stack are copied: nothing
// that takes a lock runs then. The walk (RtlLookupFunctionEntry takes the loader's function-table lock, which a thread
// suspended while a library loads holds; an allocation takes the heap's) runs on the copy once it has resumed, its
// pointers into the stack moved into the copy.
void take(Target& target, StackCopy& copy) noexcept {
    if (target.samples.size() >= kMostSamples || SuspendThread(target.thread) == static_cast<DWORD>(-1)) return;
    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    const std::int64_t qpc = galaxy::qpc_now();
    copy.size = 0u;
    DWORD64 from = 0;
    if (GetThreadContext(target.thread, &context)) {
        // The stack's committed pages from the stack pointer up (one region: NtQueryVirtualMemory, no user-mode lock).
        MEMORY_BASIC_INFORMATION region{};
        from = context.Rsp;
        if (VirtualQuery(reinterpret_cast<const void*>(from), &region, sizeof(region)) == sizeof(region) &&
            region.State == MEM_COMMIT) {
            const DWORD64 end = reinterpret_cast<DWORD64>(region.BaseAddress) + region.RegionSize;
            copy.size = static_cast<std::size_t>(std::min<DWORD64>(kStackCopyBytes, end - from));
            std::memcpy(copy.bytes.data(), reinterpret_cast<const void*>(from), copy.size);
        }
    }
    ResumeThread(target.thread);
    if (copy.size < 8u) return;
    const auto base = reinterpret_cast<DWORD64>(copy.bytes.data());
    const auto moved = [&](DWORD64 value) { return value >= from && value < from + copy.size ? value - from + base : value; };
    for (std::size_t at = 0; at + 8u <= copy.size; at += 8u) {
        DWORD64 value = 0;
        std::memcpy(&value, copy.bytes.data() + at, 8u);
        value = moved(value);
        std::memcpy(copy.bytes.data() + at, &value, 8u);
    }
    for (DWORD64* reg : {&context.Rsp, &context.Rbp, &context.Rbx, &context.Rsi, &context.Rdi, &context.R12, &context.R13,
                         &context.R14, &context.R15}) {
        *reg = moved(*reg);
    }
    Frames frames{};
    const int depth = walk(context, copy, frames);
    if (depth > 0) {
        try {
            target.samples.push_back({qpc, depth, frames});
        } catch (...) {
        }
    }
}

HANDLE open_sampled(DWORD thread_id) noexcept {
    return OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, thread_id);
}

void write(const Target& target) {
    const fs::path folder = galaxy::nebula_diagnostics_directory();
    const fs::path name =
        folder / (std::string(target.name) + "-profile-" + std::to_string(galaxy::current_process_id()) + ".txt");
    std::FILE* file = folder.empty() ? nullptr : _wfopen(name.c_str(), L"wb");
    if (file == nullptr) return;
    std::array<HMODULE, 512> modules{};
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(sizeof(modules)), &needed)) {
        for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < modules.size(); ++i) {
            MODULEINFO info{};
            char name[MAX_PATH] = {};
            if (K32GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info)) &&
                K32GetModuleBaseNameA(GetCurrentProcess(), modules[i], name, MAX_PATH) != 0) {
                std::fprintf(file, "module %llx %lx %s\n", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll)),
                             static_cast<unsigned long>(info.SizeOfImage), name);
            }
        }
    }
    for (const auto& [qpc, depth, frames] : target.samples) {
        std::fprintf(file, "sample t%llx", static_cast<unsigned long long>(qpc));
        for (int i = 0; i < depth; ++i) std::fprintf(file, " %llx", static_cast<unsigned long long>(frames[i]));
        std::fputc('\n', file);
    }
    std::fclose(file);
    char text[128];
    std::snprintf(text, sizeof(text), "%zu samples in %s-profile-%lu.txt", target.samples.size(), target.name,
                  galaxy::current_process_id());
    log_line("profile:", text);
}

// The game thread from the start; each thread that adds itself (profiled_threads::add_current) from the millisecond after.
void run(HANDLE game) {
    const auto copy = std::make_unique<StackCopy>();
    std::vector<Target> targets;
    targets.reserve(1u + galaxy::profiled_threads::kMost);
    targets.push_back({game, "game", {}});
    targets.back().samples.reserve(60'000u);
    std::size_t seen = 0u;
    auto next = Clock::now();
    while (!g_stop.load(std::memory_order_relaxed)) {
        const std::size_t claimed =
            std::min(galaxy::profiled_threads::g_claimed.load(std::memory_order_relaxed), galaxy::profiled_threads::kMost);
        while (seen < claimed) {
            const galaxy::profiled_threads::Entry& entry = galaxy::profiled_threads::g_entries[seen];
            const DWORD id = entry.id.load(std::memory_order_acquire);
            if (id == 0u) break;  // claimed, not yet complete: the next round
            ++seen;
            if (const HANDLE thread = open_sampled(id); thread != nullptr) {
                targets.push_back({thread, entry.name, {}});
                targets.back().samples.reserve(60'000u);
            }
        }
        for (Target& target : targets) take(target, *copy);
        next += std::chrono::milliseconds(1);
        sleep_until_precise(next);
    }
    for (Target& target : targets) {
        write(target);
        if (target.thread != game) CloseHandle(target.thread);
    }
}

void start(DWORD thread_id) {
    const HANDLE sampled = open_sampled(thread_id);
    if (sampled == nullptr) {
        return;
    }
    g_sampler = std::thread([sampled] {
        run(sampled);
        CloseHandle(sampled);
    });
}

// At exit: the samples so far, written.
void finish() {
    if (g_sampler.joinable()) {
        g_stop.store(true, std::memory_order_relaxed);
        g_sampler.join();
    }
}

}  // namespace game_profile
#endif

// The mods' code, started once the game is attached.
std::vector<galaxy::mods::PluginFile> gPlugins;
galaxy::mods::ModRows gModRows;
bool gModsChangeGame = false;
// The mods on this session (the scan's enabled ones): the mod log is made only when there are any.
std::uint32_t gModsOn = 0;
// Those of them a player turned on: every one but the harness's own game half (is_harness_half). With none the game's
// check of placements after the start is not called (luma_refs.h).
std::uint32_t gPlayersModsOn = 0;
// The placed packages of this start, for the start's check (modlog::check_packages).
std::vector<galaxy::modlog::StartPackage> gStartPackages;

// The two folders the host reads from (the package layout):
// - the program's folder (program_directory(), executable_path()): one for both CPU levels, since Nebula.exe and
//   Nebula-x86-64-v2.exe lie side by side. Tables, dxc\, nebula-import.exe, the restart's and the
//   crash report's program come from there.
// - module_folder(): the folder of the game module actually loaded (the program's folder, x86-64-v2\ on a CPU without
//   x86-64-v3, or --module's folder). Everything that belongs to one build of the module is read from here, never from
//   the program's folder: each CPU folder holds its own module and the files made with it.
fs::path gModuleFolder;

[[maybe_unused]] const fs::path& module_folder() {
    return gModuleFolder;
}

// The module beside the program for this CPU: x86-64-v2\ holds the build for a CPU without x86-64-v3 (native/build.ps1).
fs::path default_module_path(const fs::path& program_folder) {
#if defined(_WIN32)
    fs::path module = program_folder;
#if defined(_M_X64)
    if (!cpu_runs_x86_64_v3()) {
        module /= "x86-64-v2";
        log_line("host:", "the CPU lacks x86-64-v3: the x86-64-v2 game module");
    }
#endif
    return module / "smg_game.dll";
#elif defined(__APPLE__)
    return program_folder / "libsmg_game.dylib";
#else
    return program_folder / "libsmg_game.so";
#endif
}

void load_module(const fs::path& path) {
    std::error_code absolute_error;
    const fs::path absolute = fs::absolute(path, absolute_error);
    gModuleFolder = (absolute_error ? path : absolute).parent_path();
#if defined(_WIN32)
    void* module = LoadLibraryW(path.wstring().c_str());
#else
    // Global: find_function (a native plugin's hook) finds the game's functions here.
    void* module = dlopen(path.string().c_str(), RTLD_NOW | RTLD_GLOBAL);
#endif
    if (module == nullptr) {
        die("cannot load the game module %s", utf8_of(path.filename()).c_str());
    }
#if defined(_WIN32)
    gModule.api_fingerprint = reinterpret_cast<decltype(gModule.api_fingerprint)>(
        GetProcAddress(static_cast<HMODULE>(module), "nebula_game_api_fingerprint"));
#else
    gModule.api_fingerprint =
        reinterpret_cast<decltype(gModule.api_fingerprint)>(dlsym(module, "nebula_game_api_fingerprint"));
#endif
    gModule.attach = reinterpret_cast<decltype(gModule.attach)>(find_symbol(module, "nebula_game_attach"));
    gModule.run = reinterpret_cast<decltype(gModule.run)>(find_symbol(module, "nebula_game_run"));
    gModule.module = module;
    gModule.audio_tick = reinterpret_cast<decltype(gModule.audio_tick)>(find_symbol(module, "nebula_game_sound_frame"));
    gModule.mii_valid = reinterpret_cast<decltype(gModule.mii_valid)>(find_symbol(module, "nebula_game_mii_valid"));
    gModule.mii_changed =
        reinterpret_cast<decltype(gModule.mii_changed)>(find_symbol(module, "nebula_game_mii_changed"));
}

#if defined(NEBULA_HOST_RENDERER)


#endif

// The game: Setup's game.pak, the asset store (galaxy/asset_store.h).
struct Pak {
    char id[7] = {};  // the game id
    galaxy::assets::AssetStore store;
    fs::path store_file;
};
Pak gPak;
// A part of the game a start with mods needs, found missing at the start check's first step: every mod is off
// (docs/modding/concepts/how-nebula-loads-mods.md).
std::optional<galaxy::startcheck::MissingPart> gMissingPart;

// The harness's own game half (luma-test, placed by tools/lumacore/luma_test.py in a harness run, which sets
// LUMA_HARNESS_PLAN) is the test's instrument, not a player's mod: a start that turns every mod off (a safe start, a
// missing part) leaves it on, or no such start could be run as a test (it gives the setup and the steps, and ends the run).
bool is_harness_half(const std::string& mod_id) {
#if defined(_WIN32)
    const bool harness_run = GetEnvironmentVariableA("LUMA_HARNESS_PLAN", nullptr, 0) != 0;
#else
    const bool harness_run = std::getenv("LUMA_HARNESS_PLAN") != nullptr;
#endif
    return harness_run && mod_id == "luma-test";
}

// A safe start asked at launch: --safe-start 1, or Shift held as Nebula starts (Windows, a visible start).
bool safe_start_asked() {
    bool asked = ::galaxy::launch_args().safe_start;
#if defined(NEBULA_HOST_RENDERER)
    asked = asked || galaxy::bench::running();  // a benchmark measures the game itself: every mod off, as a safe start
#endif
#if defined(_WIN32)
    asked = asked || (!::galaxy::launch_args().started_hidden && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
#endif
    return asked;
}

bool open_pak(const fs::path& path) {
    gPak.store_file = path;
    if (!gPak.store.open(path)) {
        log_line("import:", "The game files need importing again. Open Nebula Setup and import the game.");
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
        if (!::galaxy::launch_args().started_hidden) {
            MessageBoxW(nullptr, L"Open Nebula Setup and import the game again.", L"Nebula", MB_OK | MB_ICONINFORMATION);
        }
#endif
        return false;
    }
    // The releases the module plays (NebulaRegion.hpp): North America, Europe and Japan.
    std::snprintf(gPak.id, sizeof(gPak.id), "%.6s", std::string(gPak.store.game_id()).c_str());
    if (std::strcmp(gPak.id, "RMGE01") != 0 && std::strcmp(gPak.id, "RMGP01") != 0 &&
        std::strcmp(gPak.id, "RMGJ01") != 0) {
        die("%s holds %.6s, not Super Mario Galaxy for North America, Europe or Japan", utf8_of(path.filename()).c_str(), gPak.id);
    }
    return true;
}

// The calling thread's name, as profilers, debuggers and the CPU report show it.
void name_thread(const wchar_t* name) {
#if defined(_WIN32)
    SetThreadDescription(GetCurrentThread(), name);
#else
    char narrow[16];
    std::snprintf(narrow, sizeof(narrow), "%ls", name);
#if defined(__APPLE__)
    pthread_setname_np(narrow);
#else
    pthread_setname_np(pthread_self(), narrow);
#endif
#endif
}

// A worker thread taking jobs in order.
class Worker {
public:
    explicit Worker(const wchar_t* name) : name_(name) {}

    template <typename F>
    void post(F&& job) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.emplace_back(std::forward<F>(job));
        }
        ready_.notify_one();
    }

    void start() {
        thread_ = std::thread([this] {
            name_thread(name_);
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, [this] { return !jobs_.empty(); });
                    job = std::move(jobs_.front());
                    jobs_.pop_front();
                }
                job();
            }
        });
        thread_.detach();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    std::thread thread_;
    const wchar_t* name_;
};

Worker gFs(L"Nebula Host Saves");

// The save folder's root: the game's paths under it, each checked.
fs::path gSaveRoot;

// The game's save folder under it (its four-character code); gOldSaveDataPath is where earlier versions kept the
// save (title/00010000/<the code in hex>/data), read once; a mod's clone copies it too).
std::string gSaveDataPath;
// The co-op save being played (galaxy::coop::SaveStore): the game's save folder while set. The fs thread reads it.
std::mutex gCoopSaveMutex;
fs::path gCoopSaveFolder;

// Where the game's save folder is instead of the save root's: the co-op save's.
fs::path save_folder_override() {
    const std::lock_guard<std::mutex> lock(gCoopSaveMutex);
    return gCoopSaveFolder;
}

std::string gOldSaveDataPath;
// A mod's own save folder (Riivolution's savegame): the game's save files go there instead.
fs::path gModSaveRoot;

bool save_folder_path(const char* path, fs::path* out) {
    if (path == nullptr || path[0] != '/') {
        return false;
    }
    fs::path result = !gModSaveRoot.empty() && !gSaveDataPath.empty() && (std::string_view(path).starts_with(gSaveDataPath) || std::string_view(path).starts_with(gOldSaveDataPath))
                          ? gModSaveRoot
                          : gSaveRoot;
    // The co-op save's folder over a mod's (save_folder_override).
    fs::path saves;
    if (!gSaveDataPath.empty() && std::strncmp(path, gSaveDataPath.c_str(), gSaveDataPath.size()) == 0 &&
        (path[gSaveDataPath.size()] == '/' || path[gSaveDataPath.size()] == '\0') &&
        !(saves = save_folder_override()).empty()) {
        result = saves;
        path += gSaveDataPath.size();
        if (*path == '\0') {
            *out = result;
            return true;
        }
    }
    for (const fs::path& part : fs::path(path + 1)) {
        // A part with a drive or a root would replace the folder instead of joining it.
        if (part == ".." || part == "." || part.has_root_name() || part.has_root_directory()) {
            return false;
        }
        result /= part;
    }
    *out = result;
    return true;
}

// The sound clock: the game starts it with its frame's length (sound ticks a second); the lock guards the running
// flag and the rate, and the clock thread waits on the condition variable until both are set.
std::mutex gSoundClockLock;
std::condition_variable gSoundClockWake;
uint32_t gSoundTicksPerSecond = 0;
bool gSoundClockRunning = false;

#if defined(NEBULA_HOST_RENDERER)
extern galaxy::host::NativeGameAudio* gAudio;
#endif

// The app out of sight (SDL hosts: Android's background): the display and audio clocks stop, so the game waits at
// its next frame; they restart from its return, with nothing to catch up.
std::mutex gSuspendLock;
std::condition_variable gSuspendWake;
bool gSuspended = false;  // guarded by gSuspendLock

// The window lost focus and the player chose PAUSE (runtime settings, focus_loss): set when it is lost, taken by the game
// once it can open its pause menu (svc_take_pause_request), dropped when the window is in front again unless it was minimised
// (the game is held then and takes it at its first frame after the return).
std::atomic<bool> gPauseRequest{false};

#if defined(NEBULA_HOST_RENDERER)
// ON LOSING FOCUS (runtime settings focus_loss) for a desktop window, called from the main loop with whether the window is
// not in front (`away`, minimised included) and whether it is minimised now. PAUSE has the game open its pause menu (the
// pause button's path, so its timers stop), MUTE silences the sound (the mixer reads background_muted) and the game goes
// on; KEEP RUNNING does nothing. Minimising holds the game whatever is chosen.
void follow_focus_loss(bool away, bool minimised_now) {
    static bool was_away = false;
    static bool away_minimised = false;
    static std::chrono::steady_clock::time_point lapse{};
    const galaxy::FocusLoss focus = galaxy::get_runtime_settings().focus_loss;
    if (away && !was_away && focus == galaxy::FocusLoss::Pause) {
        gPauseRequest.store(true, std::memory_order_release);
    }
    if (away) {
        away_minimised = away_minimised || minimised_now;
    }
    if (!away && was_away) {
        if (!away_minimised) {
            gPauseRequest.store(false, std::memory_order_release);
        } else {
            // Held while minimised: the game takes the request at its first frame back; a game that cannot pause then
            // (a cutscene) lets it lapse rather than pause long after the player returned.
            lapse = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        }
        away_minimised = false;
    }
    if (focus != galaxy::FocusLoss::Pause && !away_minimised) {
        gPauseRequest.store(false, std::memory_order_release);
    }
    if (!away && lapse != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() >= lapse) {
        gPauseRequest.store(false, std::memory_order_release);
        lapse = std::chrono::steady_clock::time_point{};
    }
    if (away != was_away) {
        log_line("host:", away ? "window not in front" : "window in front");
    }
    was_away = away;
    galaxy::set_background_muted(away && focus == galaxy::FocusLoss::Mute);
}
#endif

// When the player last gave the game an input (a button, a stick pushed, the pointer moved) and when the game last played a
// movie's sound, in steady-clock milliseconds (power_policy.h): the host's main loop tells the OS whether anyone is playing.
// 0 until the first; the start counts as an input.
#if defined(NEBULA_HOST_RENDERER)
std::atomic<int64_t> gLastInputMs{0};
std::atomic<int64_t> gLastMovieMs{-1};

int64_t steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#endif

// The start runs ahead: the game shows nothing until its first 3D scene (the title's, or a test's galaxy), and its
// loads advance a step each frame while it takes well under a millisecond a frame, so until then frames come every
// kBootPeriod and one frame a display period is presented. The first 3D scene, or kBootFrames frames, end it.
constexpr auto kBootPeriod = std::chrono::microseconds(2000);
constexpr uint32_t kBootFrames = 600;
std::atomic<bool> gBootFast{true};

void end_boot_fast(const char* why) {
    if (gBootFast.exchange(false, std::memory_order_relaxed)) {
        boot_mark(why);
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::set_frame_end_presents(true);
#endif
    }
}

// An extension's fixed steps (NEBULA_EXT_FIXED_STEPS): the game's time is its frame count. The host's frame and audio
// clocks do not run; the game takes each frame when it has nothing to run (test_frame_step), disc reads complete inside
// their call, and the steady clock counts frames. Two runs of one build then match frame for frame.
bool deterministic_mode() {
    return (galaxy::extensions::flags() & NEBULA_EXT_FIXED_STEPS) != 0u;
}
std::atomic<uint64_t> gDeterministicFrames{0};

// True when it waited.
bool wait_while_suspended() {
    std::unique_lock<std::mutex> lock(gSuspendLock);
    if (!gSuspended) {
        return false;
    }
    gSuspendWake.wait(lock, [] { return !gSuspended; });
    return true;
}

#if !defined(NEBULA_HOST_RENDERER)
// Headless, nothing is mixed: the game's sound logic runs on the clock.
void sound_clock_thread() {
    if (deterministic_mode()) {
        return;  // the audio ticks come with each frame (svc_test_frame_step)
    }
    name_thread(L"Nebula Host Audio");
    auto next = std::chrono::steady_clock::now();
    for (;;) {
        uint32_t ticks_per_second = 0;
        {
            std::unique_lock<std::mutex> lock(gSoundClockLock);
            gSoundClockWake.wait(lock, [] { return gSoundClockRunning && gSoundTicksPerSecond != 0; });
            ticks_per_second = gSoundTicksPerSecond;
        }
        gSoundBlocks.fetch_add(1, std::memory_order_relaxed);
        gModule.audio_tick();
        next += std::chrono::nanoseconds(static_cast<int64_t>(1e9 / ticks_per_second));
        std::this_thread::sleep_until(next);
    }
}
#endif

// The device asks for the game's next frame of sound, on its thread.
void audio_frame_tick() {
    gSoundBlocks.fetch_add(1, std::memory_order_relaxed);
    gModule.audio_tick();
}

#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
// The display's refreshes by the compositor's clock, looked at twice a second.
std::optional<galaxy::host::DisplayRefresh> display_refresh() {
    static std::optional<galaxy::host::DisplayRefresh> known;
    static int64_t earlier_vblank_ns = 0;
    static uint32_t frames = 0;
    if (frames++ % 30u != 0u) return known;
    known.reset();
    DWM_TIMING_INFO timing{};
    timing.cbSize = sizeof(timing);
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)) && timing.qpcRefreshPeriod != 0u) {
        const auto to_ns = [&](uint64_t qpc) {
            const uint64_t whole = qpc / static_cast<uint64_t>(frequency.QuadPart);
            const uint64_t part = qpc % static_cast<uint64_t>(frequency.QuadPart);
            return static_cast<int64_t>(whole * 1000000000ull + part * 1000000000ull / static_cast<uint64_t>(frequency.QuadPart));
        };
        const galaxy::host::DisplayRefresh now{to_ns(timing.qpcVBlank), to_ns(timing.qpcRefreshPeriod)};
        if (earlier_vblank_ns != 0 && galaxy::host::refreshes_in_step(earlier_vblank_ns, now)) known = now;
        earlier_vblank_ns = now.vblank_ns;
    }
    return known;
}

// The next frame's start after the one due at `start` (frame_phase.h).
Clock::time_point next_frame_start(Clock::time_point start, std::chrono::nanoseconds period) {
    static galaxy::host::FramePhase phase;
    static int64_t previous_ns = 0;
    const int64_t start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count();
    const int64_t present_ns = galaxy::render::g_last_present_ns.load(std::memory_order_relaxed);
    const int64_t work_ns = previous_ns != 0 && present_ns > previous_ns ? present_ns - previous_ns : 0;
    previous_ns = start_ns;
    const int64_t next_ns = phase.next_start(start_ns, work_ns, period.count(), display_refresh());
    return start + std::chrono::nanoseconds(next_ns - start_ns);
}

// Tells the OS about the player's presence (power_policy.h) when it changes: the process asks for efficiency (EcoQoS) while the
// window is behind another, minimised, or left alone for a minute with no cutscene; the display is held on only while the window
// is in front and someone plays or watches. Called from the host's main loop, which is also the thread that holds the display.
void apply_power_wish(bool background, bool minimised) {
    static galaxy::host::PowerWish applied;  // the OS's state at the start: full speed, the display's own timers
    static const int64_t started_ms = steady_ms();  // the start counts as an input
    const int64_t now_ms = steady_ms();
    const galaxy::host::PowerFacts facts{now_ms, std::max(gLastInputMs.load(std::memory_order_relaxed), started_ms),
                                         gLastMovieMs.load(std::memory_order_relaxed), background, minimised};
    const galaxy::host::PowerWish wish = galaxy::host::power_wish(facts);
    if (wish == applied) {
        return;
    }
    if (wish.efficient != applied.efficient) {
        galaxy::host::set_process_efficiency(wish.efficient);
        log_line("[power]", wish.efficient ? "efficiency requested (away)" : "full speed (in front)");
    }
    if (wish.display_required != applied.display_required) {
        galaxy::host::keep_display_on(wish.display_required);
        log_line("[power]", wish.display_required ? "display held on (playing)" : "display left to the OS's timers");
    }
    applied = wish;
}
#endif

// The display's frame at 60 Hz: 59.94 would repeat a frame every 17 s on a 60 Hz display. A late
// wake-up skips the fields it missed, as a display does, rather than delivering them back to back. On Android the
// frame follows the display's own vsync (every second one at 120 Hz), so frames never drift against it and each
// stays on screen for the same time; the host's clock takes over where the display gives none.
void svc_frame_wait(void*) {
    const auto display_period = std::chrono::nanoseconds(galaxy::kGameStepNs);
    static auto period = gBootFast.load(std::memory_order_relaxed) ? std::chrono::nanoseconds(kBootPeriod) : display_period;
    static auto next = Clock::now() + period;
#if defined(NEBULA_HOST_SDL)
    static int64_t vsync_ns = 0;
    static uint32_t on_vsync = 0;
    static uint32_t frames = 0;
    static int64_t work_sum = 0;
    static int64_t work_max = 0;
#endif
    for (;;) {
        // The step's moment on the runtime's clock: the display's vsync it waited for, else the host clock's due time,
        // reached by a precise wait (precise_wait.h: a timer woken late would start the step late).
        std::int64_t due_qpc = 0;
#if defined(NEBULA_HOST_SDL)
        const bool vsync = galaxy::sdl::wait_display_frame(period.count(), vsync_ns);
        if (vsync) {
            due_qpc = vsync_ns;
        } else {
            due_qpc = galaxy::qpc_of(next);
            galaxy::PreciseWait::for_this_thread().until(due_qpc);
        }
#else
        due_qpc = galaxy::qpc_of(next);
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
        galaxy::timeline::record(galaxy::timeline::Event::StepWaitBegin, static_cast<std::uint64_t>(due_qpc), 0u);
#endif
        galaxy::PreciseWait::for_this_thread().until(due_qpc);
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
        galaxy::timeline::record(galaxy::timeline::Event::StepWaitEnd, 0u, 0u);
#endif
#endif
        if (wait_while_suspended()) {
            next = Clock::now() + period;
            continue;
        }
        // The last frame's work (the game and render threads' CPU time), to the performance hints (Android).
        [[maybe_unused]] const int64_t work = galaxy::host::perf_hint_frame_done();
        if (period != display_period && !gBootFast.load(std::memory_order_relaxed)) {
            period = display_period;
        }
        // The hints' budget is the frame the clock keeps now (the short boot frame, then the game step).
        galaxy::host::perf_hint_set_target(period.count());
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
        next = next_frame_start(next, period);
#else
        next += period;
#endif
        const auto now = Clock::now();
        if (now >= next) {
            next = now + period;
        }
#if defined(NEBULA_HOST_SDL)
        if (vsync) {
            next = now + period;  // the host's clock continues from the display's
            ++on_vsync;
        }
        // Every 10 s: how the frames were paced, and the frame work (the busier of the game and render threads).
        work_sum += work;
        work_max = std::max(work_max, work);
        if (++frames == 600u) {
            std::fprintf(stderr, "[pacing] %u frames, %u on the display's vsync (%.1f Hz); frame work %.2f ms average, "
                         "%.2f ms longest\n", frames, on_vsync, galaxy::sdl::display_refresh_hz(),
                         static_cast<double>(work_sum) / frames / 1e6, static_cast<double>(work_max) / 1e6);
            frames = on_vsync = 0u;
            work_sum = work_max = 0;
        }
#endif
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::set_step_moment(due_qpc);
#else
        (void)due_qpc;
#endif
        return;
    }
}

// Deterministic mode's frame: the game has nothing to run. The audio ticks of one frame's length (the audio clock's
// period, without the device's correction) and the clock.
void svc_test_frame_step(void*) {
    static double due_ns = 0.0;
    uint32_t ticks_per_second = 0;
    {
        std::lock_guard<std::mutex> lock(gSoundClockLock);
        ticks_per_second = gSoundClockRunning ? gSoundTicksPerSecond : 0u;
    }
    if (ticks_per_second != 0u) {
        due_ns += 1e9 / galaxy::kGameStepRate;
        const double period_ns = 1e9 / ticks_per_second;
        while (due_ns >= period_ns) {
            due_ns -= period_ns;
            gSoundBlocks.fetch_add(1, std::memory_order_relaxed);
            gModule.audio_tick();
        }
    } else {
        due_ns = 0.0;
    }
    gDeterministicFrames.fetch_add(1, std::memory_order_relaxed);
}

void svc_log(void*, const char* text) {
    log_line("game:", text);
}

#if defined(_WIN32)
// --diagnostics thread-cpu: each thread's CPU time and context switches over the last second, by name (or,
// unnamed, the module and offset it started at), busiest first; threads under 0.5 ms/s are counted in the total only.
// The report's own cost (a snapshot of every thread in the system) is left out of the reporting thread's figure.
void report_thread_cpu(double tsc_hz) {
    static std::unordered_map<DWORD, uint64_t> last;
    static std::unordered_map<DWORD, uint32_t> last_switches;
    static uint64_t report_cycles = 0;
    ULONG64 report_start = 0;
    QueryThreadCycleTime(GetCurrentThread(), &report_start);
    // The process's threads and their context switches: NtQuerySystemInformation(SystemProcessInformation). On x64
    // each process record is 0x100 bytes followed by 0x50-byte thread records (thread id at +0x30, switches at +0x40).
    using SystemQueryFn = LONG(WINAPI*)(int, void*, ULONG, ULONG*);
    static const auto system_query = reinterpret_cast<SystemQueryFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation")));
    static std::vector<uint8_t> buffer(4u << 20);
    struct ThreadSample {
        DWORD id;
        uint32_t switches;
    };
    std::vector<ThreadSample> threads;
    ULONG needed = 0;
    LONG status = system_query != nullptr ? system_query(5, buffer.data(), (ULONG)buffer.size(), &needed) : -1;
    if (status == static_cast<LONG>((3u << 30) | 4u) /* STATUS_INFO_LENGTH_MISMATCH */ && needed != 0) {
        buffer.resize(needed + (1u << 20));
        status = system_query(5, buffer.data(), (ULONG)buffer.size(), &needed);
    }
    if (status != 0) {
        return;
    }
    const DWORD pid = GetCurrentProcessId();
    for (size_t offset = 0;;) {
        const uint8_t* process = buffer.data() + offset;
        uint32_t next = 0, count = 0;
        uintptr_t process_id = 0;
        std::memcpy(&next, process, 4);
        std::memcpy(&count, process + 4, 4);
        std::memcpy(&process_id, process + 0x50, sizeof(process_id));
        if (process_id == pid) {
            for (uint32_t i = 0; i < count; ++i) {
                const uint8_t* thread = process + 0x100 + i * 0x50;
                uintptr_t thread_id = 0;
                uint32_t switches = 0;
                std::memcpy(&thread_id, thread + 0x30, sizeof(thread_id));
                std::memcpy(&switches, thread + 0x40, 4);
                threads.push_back({(DWORD)thread_id, switches});
            }
            break;
        }
        if (next == 0) {
            break;
        }
        offset += next;
    }
    using QueryFn = LONG(WINAPI*)(HANDLE, int, void*, ULONG, ULONG*);
    static const auto query = reinterpret_cast<QueryFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread")));
    struct Row {
        double ms;
        std::string name;
        uint32_t switches;
    };
    std::vector<Row> rows;
    double total = 0.0;
    const DWORD self = GetCurrentThreadId();
    for (const ThreadSample& sample : threads) {
        const DWORD thread_id = sample.id;
        const HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, thread_id);
        if (thread == nullptr) {
            continue;
        }
        ULONG64 cycles = 0;
        QueryThreadCycleTime(thread, &cycles);
        if (thread_id == self) {
            cycles = report_start - report_cycles;
        }
        std::string name;
        PWSTR description = nullptr;
        if (SUCCEEDED(GetThreadDescription(thread, &description)) && description != nullptr && description[0] != 0) {
            // A description longer than the buffer converts to nothing: the thread then shows by its start address.
            char narrow[64] = {};
            if (WideCharToMultiByte(CP_UTF8, 0, description, -1, narrow, sizeof(narrow), nullptr, nullptr) > 0) {
                name = narrow;
            }
        }
        LocalFree(description);
        void* start = nullptr;
        HMODULE owner = nullptr;
        if (name.empty()) {
            // ThreadQuerySetWin32StartAddress (9).
            char where[160] = "?";
            if (query != nullptr && query(thread, 9, &start, sizeof(start), nullptr) == 0 &&
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   static_cast<LPCWSTR>(start), &owner)) {
                char path[MAX_PATH];
                const DWORD length = GetModuleFileNameA(owner, path, sizeof(path));
                const char* file = length != 0 ? std::strrchr(path, '\\') : nullptr;
                std::snprintf(where, sizeof(where), "%s+0x%llx", file != nullptr ? file + 1 : "?",
                              (unsigned long long)(static_cast<char*>(start) - reinterpret_cast<char*>(owner)));
            }
            name = "tid " + std::to_string(thread_id) + " " + where;
        }
        CloseHandle(thread);
        uint64_t& before = last[thread_id];
        const double ms = (double)(cycles - before) * 1000.0 / tsc_hz;
        before = cycles;
        total += ms;
        uint32_t& switches_before = last_switches[thread_id];
        const uint32_t switches = sample.switches - switches_before;
        switches_before = sample.switches;
        if (ms >= 0.5) {
            rows.push_back({ms, std::move(name), switches});
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.ms > b.ms; });
    std::string line = "total " + std::to_string((int)(total + 0.5)) + " ms/s";
    for (const Row& row : rows) {
        char item[160];
        std::snprintf(item, sizeof(item), "; %s %.1f (%u sw)", row.name.c_str(), row.ms, row.switches);
        line += item;
    }
    log_line("cpu:", line.c_str());
    ULONG64 report_end = 0;
    QueryThreadCycleTime(GetCurrentThread(), &report_end);
    report_cycles += report_end - report_start;
}
#endif

std::string gSceneStage;

#if defined(_WIN32)
// Crash reports. A fatal exception in the game module or the host, or the game's own fatal call, writes a minidump and
// the report (report_crash_facts) to the crash folder from a thread kept for it (the failing thread's stack may be what
// failed), logs where it happened and the game's threads, and ends the process.
using MiniDumpWriteDumpFn = decltype(&MiniDumpWriteDump);  // loaded at the crash: dbghelp.dll is not linked

HANDLE gCrashRequest;
HANDLE gCrashDone;
EXCEPTION_POINTERS* gCrashInfo;
DWORD gCrashThread;
std::atomic<int> gCrashing{0};
std::atomic<DWORD> gCrashWriterThread{0};
// The fault is outside the host and the game module (a graphics driver, a system library, a jump to no code): its
// dump keeps the process's private memory too, the heaps where that code's objects live (driver-pso-crash.md: a
// driver object's table read 0xFF, which stacks alone cannot explain). Set before gCrashRequest.
bool gCrashForeign = false;
constexpr DWORD kHeapCorruption = 0xC0000374u;  // STATUS_HEAP_CORRUPTION (ntstatus.h, which windows.h leaves out)

// After the dump: the report beside it, the progress written, the crashed thread let go, and the process ended.
// Defined with the co-op session, below.
void leave_coop_room_at_crash();

[[noreturn]] void finish_crash_report(const fs::path& dump) {
    report_crash_facts(dump);
    // The dump and report first (what the crash left must not cost them): then the room hears this player go, so the
    // others don't wait for it to stop answering.
    leave_coop_room_at_crash();
#if defined(NEBULA_HOST_RENDERER)
    flush_progress_bounded(1500u);
#endif
    SetEvent(gCrashDone);
    // The dump and report are written: whatever the crashed thread still does (a flush behind a lock another thread
    // held when it stopped) never keeps the process alive.
    Sleep(5000);
    TerminateProcess(GetCurrentProcess(), 4);
    std::_Exit(4);
}

void crash_writer() {
    gCrashWriterThread.store(GetCurrentThreadId());
    for (;;) {
        WaitForSingleObject(gCrashRequest, INFINITE);
        // Stacks and what they point at (the module's data segments would add its half-gigabyte image window); for a
        // fault outside our code, the private memory and the handles as well. A region that cannot be read (freed or
        // made inaccessible while the dump is written) is left out, not the end of the dump: without it a wide dump
        // stopped at ERROR_PARTIAL_COPY with no module list (p0479 kit-replay).
        const unsigned dump_type = MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpIgnoreInaccessibleMemory |
                                   (gCrashForeign ? MiniDumpWithPrivateReadWriteMemory | MiniDumpWithHandleData : 0u);
        // The watcher writes the dump from outside (crash_watch.h), asked with no heap and no lock of this process.
        static wchar_t watched[1024];
        DWORD watch_error = 0;
        DWORD watch_snapshot = 0;
        const galaxy::crash_watch::Result watch = galaxy::crash_watch::write_dump(
            gCrashThread, gCrashInfo, dump_type, watched, sizeof(watched) / sizeof(watched[0]), &watch_error, &watch_snapshot, 30000u);
        if (watch != galaxy::crash_watch::Result::kNoWatcher) {
            const fs::path path(watched);
            char text[MAX_PATH + 64];
            if (watch == galaxy::crash_watch::Result::kWritten) {
                std::snprintf(text, sizeof(text), "minidump %ls", path.filename().c_str());
            } else {
                std::snprintf(text, sizeof(text), "minidump failed (0x%08lX): %ls", watch_error, path.filename().c_str());
            }
            log_line("crash:", text);
            // How it was read: a snapshot (the process held still), or the live process and why no snapshot.
            if (watch_snapshot == 0) {
                std::snprintf(text, sizeof(text), "the dump read a snapshot of the process");
            } else {
                std::snprintf(text, sizeof(text), "the dump read the live process: no snapshot (0x%08lX)", watch_snapshot);
            }
            log_line("crash:", text);
            finish_crash_report(path);
        }
        const fs::path folder = crash_folder();
        std::error_code ignored;
        fs::create_directories(folder, ignored);
        SYSTEMTIME now;
        GetLocalTime(&now);
        // The file name is fixed width (a date, a time and a process id); the folder is any length.
        wchar_t dump_name[64];
        std::swprintf(dump_name, sizeof(dump_name) / sizeof(dump_name[0]), L"nebula-native-%04u%02u%02u-%02u%02u%02u-%lu.dmp", now.wYear,
                      now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
        const std::wstring path = (folder / dump_name).wstring();
        const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
        const auto write = dbghelp != nullptr
                               ? reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"))
                               : nullptr;
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (write != nullptr && file != INVALID_HANDLE_VALUE) {
            // No watcher (it could not start): written here, which can stop on a lock a suspended thread holds.
            MINIDUMP_EXCEPTION_INFORMATION exception{gCrashThread, gCrashInfo, FALSE};
            const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, static_cast<MINIDUMP_TYPE>(dump_type),
                                  gCrashInfo != nullptr ? &exception : nullptr, nullptr, nullptr);
            char text[MAX_PATH + 32];
            std::snprintf(text, sizeof(text), "%s %ls", ok ? "minidump" : "minidump failed:", fs::path(path).filename().c_str());
            log_line("crash:", text);
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        finish_crash_report(fs::path(path));
    }
}

void report_crash(EXCEPTION_POINTERS* info) {
    if (gCrashRequest == nullptr) {
        return;
    }
    gCrashInfo = info;
    gCrashThread = GetCurrentThreadId();
    SetEvent(gCrashRequest);
    WaitForSingleObject(gCrashDone, 60000);
    std::fflush(stdout);
}

// The mod whose plugin's code was running at a fault: the fault's own place, else the nearest plugin frame the stack
// unwinds to from it (the game's code a plugin's call reached). Null: none, or the stack can't be read.
__declspec(noinline) const char* plugin_in_fault(const EXCEPTION_POINTERS* info, bool* in_plugin) noexcept {
    if (const char* mod = galaxy::mods::plugin_at(info->ExceptionRecord->ExceptionAddress)) {
        *in_plugin = true;
        return mod;
    }
    *in_plugin = false;
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
        return nullptr;
    }
    const char* found = nullptr;
    __try {
        CONTEXT context = *info->ContextRecord;
#if defined(_M_ARM64)
        DWORD64& pc = context.Pc;
#else
        DWORD64& pc = context.Rip;
#endif
        for (int frame = 0; frame < 64 && pc != 0 && found == nullptr; ++frame) {
            DWORD64 image = 0;
            const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(pc, &image, nullptr);
            if (function == nullptr) {
                // A leaf function: its return address is where the stack (or the link register) holds it.
#if defined(_M_ARM64)
                pc = context.Lr;
#else
                pc = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
#endif
            } else {
                void* handler_data = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, pc, function, &context, &handler_data, &establisher, nullptr);
            }
            found = galaxy::mods::plugin_at(reinterpret_cast<const void*>(pc));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        found = nullptr;
    }
    return found;
}

// The crash's mod, when a plugin's code was running (gCrashFacts after gather_crash_facts).
void note_crash_plugin(const EXCEPTION_POINTERS* info) noexcept {
    // A fault the guard saw in a mod's code and did not resume: its sentence, and the mod is the guard's.
    if (const char* guard = galaxy::luma_guard::note_text(info->ExceptionRecord->ExceptionAddress)) {
        log_line("crash: guard:", guard);
        std::snprintf(gCrashPlugin, sizeof(gCrashPlugin), "%s", galaxy::luma_guard::note_mod());
        gCrashFacts.plugin_mod = gCrashPlugin;
        gCrashFacts.in_plugin = true;
        return;
    }
    bool in_plugin = false;
    if (const char* mod = plugin_in_fault(info, &in_plugin)) {
        std::snprintf(gCrashPlugin, sizeof(gCrashPlugin), "%s", mod);
        gCrashFacts.plugin_mod = gCrashPlugin;
        gCrashFacts.in_plugin = in_plugin;
        char line[192];
        std::snprintf(line, sizeof(line), in_plugin ? "in the code of the mod %s" : "in a call the code of the mod %s made", gCrashPlugin);
        log_line("crash:", line);
    }
}

// The report, out of line: the filter below runs for every exception in the process and keeps its frame small.
[[noreturn]] __declspec(noinline) void report_fatal_exception(EXCEPTION_POINTERS* info, HMODULE owner) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    const void* address = info->ExceptionRecord->ExceptionAddress;
    wchar_t name[MAX_PATH] = {};
    GetModuleFileNameW(owner, name, MAX_PATH);
    char text[MAX_PATH + 160];
    std::snprintf(text, sizeof(text), "exception 0x%08lX at %ls+0x%llx (thread %lu), address 0x%llx", code,
                  fs::path(name).filename().c_str(),
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(owner)),
                  GetCurrentThreadId(),
                  static_cast<unsigned long long>(info->ExceptionRecord->NumberParameters >= 2
                                                      ? info->ExceptionRecord->ExceptionInformation[1]
                                                      : 0));
    log_line("crash:", text);
    log_line("crash: stage", gSceneStage.c_str());
    gather_crash_facts(galaxy::crash::Facts::Kind::Fault, text, owner == static_cast<HMODULE>(gModule.module),
                       code == EXCEPTION_STACK_OVERFLOW);
    note_crash_plugin(info);
    report_crash(info);
    TerminateProcess(GetCurrentProcess(), 4);
    std::_Exit(4);
}

// Set while describe_cpp_exception reads a record: a fault there belongs to its own __except, which the vectored
// handler would otherwise see first and report as the crash.
thread_local bool tDescribingException = false;

// A C++ exception's record (MSVC's 0xE06D7363: the object, its ThrowInfo, the image base its offsets start from):
// the thrown type and, for a std::exception, what(). A rethrow (`throw;`) has neither object nor ThrowInfo. Read
// under SEH: the record comes from a failing process.
void describe_cpp_exception(const EXCEPTION_RECORD* record, char* out, std::size_t size) {
    std::snprintf(out, size, "uncaught C++ exception");
    if (record->NumberParameters < 4u) {
        return;
    }
    if (record->ExceptionInformation[1] == 0u || record->ExceptionInformation[2] == 0u) {
        std::snprintf(out, size, "uncaught C++ exception (rethrown)");
        return;
    }
    tDescribingException = true;
    __try {
        const auto base = static_cast<uintptr_t>(record->ExceptionInformation[3]);
        const auto* object = reinterpret_cast<const char*>(record->ExceptionInformation[1]);
        // ThrowInfo: attributes, unwind, forward compat, catchable types (32-bit offsets from the image base).
        const auto* throw_info = reinterpret_cast<const int32_t*>(record->ExceptionInformation[2]);
        const auto* types = reinterpret_cast<const int32_t*>(base + static_cast<uint32_t>(throw_info[3]));
        const char* thrown = nullptr;
        const char* what = nullptr;
        for (int32_t i = 0; i < types[0] && i < 16; ++i) {
            // CatchableType: properties, type descriptor, this displacement (mdisp, pdisp, vdisp), size, copy.
            const auto* type = reinterpret_cast<const int32_t*>(base + static_cast<uint32_t>(types[1 + i]));
            const char* name = reinterpret_cast<const char*>(base + static_cast<uint32_t>(type[1])) + 16;
            if (thrown == nullptr) {
                thrown = name;
            }
            if (std::strcmp(name, ".?AVexception@std@@") == 0) {
                what = reinterpret_cast<const std::exception*>(object + type[2])->what();
            }
        }
        std::snprintf(out, size, "uncaught C++ exception %s: %s", thrown != nullptr ? thrown : "?",
                      what != nullptr ? what : "(not a std::exception)");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    tDescribingException = false;
}

#if defined(__clang__)
__attribute__((no_sanitize("address", "undefined")))
#endif
LONG CALLBACK crash_handler(EXCEPTION_POINTERS* info) {
    if (tDescribingException) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    // A C++ exception, logged where it is thrown (the first 32): one thrown on the game's own stacks cannot unwind
    // through them and ends the process before any filter runs. Handled or not, the line says what was thrown.
    if (code == 0xE06D7363u) {
        static std::atomic<unsigned> logged{0u};
        if (logged.fetch_add(1u, std::memory_order_relaxed) < 32u) {
            char text[512];
            describe_cpp_exception(info->ExceptionRecord, text, sizeof(text));
            char line[640];
            std::snprintf(line, sizeof(line), "thrown: %s (thread %lu)", text + std::strlen("uncaught "),
                          GetCurrentThreadId());
            log_line("exception:", line);
            void* frames[16];
            const USHORT count = RtlCaptureStackBackTrace(1, 16, frames, nullptr);
            const auto exe = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            for (USHORT f = 0; f < count; ++f) {
                const auto at = reinterpret_cast<std::uintptr_t>(frames[f]);
                std::snprintf(line, sizeof(line), "Nebula.exe+0x%llx", static_cast<unsigned long long>(at - exe));
                log_line("thrown from:", line);
            }
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // STATUS_HEAP_CORRUPTION: a heap found its own records broken. ntdll raises it inside its own __except, whose
    // filter ends the process without calling the unhandled-exception filter, so this handler is the only code of the
    // process that sees it. It is raised from ntdll whichever code broke the heap, and the process ends after it
    // whoever handles it: reported here for any module, with the private memory (the heaps) in the dump.
    if (code == kHeapCorruption) {
        HMODULE owner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                static_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress), &owner)) {
            owner = GetModuleHandleW(L"ntdll.dll");
        }
        if (gCrashing.exchange(1) != 0) {
            // Another fault is being written: passed on, this one would end the process mid-write. The writer's own
            // is passed on (nothing would be left to wait for).
            if (GetCurrentThreadId() != gCrashWriterThread.load() && gCrashDone != nullptr) {
                WaitForSingleObject(gCrashDone, 60000);
                Sleep(INFINITE);
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }
        gCrashForeign = true;
        report_fatal_exception(info, owner);
    }
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_INT_OVERFLOW &&
        code != EXCEPTION_IN_PAGE_ERROR) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // Only faults in the game module or this executable: drivers and system libraries handle their own.
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress), &owner) ||
        (owner != GetModuleHandleW(nullptr) && owner != static_cast<HMODULE>(gModule.module) &&
         galaxy::mods::plugin_at(info->ExceptionRecord->ExceptionAddress) == nullptr)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (gCrashing.exchange(1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    report_fatal_exception(info, owner);
}

// abort(), which std::terminate calls for an uncaught C++ exception: no exception filter sees it. Logs what was
// thrown, when that is why, and writes the dump from the aborting thread's context.
void abort_handler(int) {
    const char* text = "abort";
    std::string what;
    if (const std::exception_ptr active = std::current_exception()) {
        try {
            std::rethrow_exception(active);
        } catch (const std::exception& error) {
            what = std::string("uncaught exception: ") + error.what();
            text = what.c_str();
        } catch (...) {
            text = "uncaught exception of unknown type";
        }
    }
    log_line("crash:", text);
    if (gCrashing.exchange(1) == 0) {
        gather_crash_facts(galaxy::crash::Facts::Kind::Abort, text);
        CONTEXT context{};
        RtlCaptureContext(&context);
        EXCEPTION_RECORD record{};
        record.ExceptionCode = 0xE0000003u;  // not a system code: marks the dump as an abort
        record.ExceptionAddress = _ReturnAddress();
        EXCEPTION_POINTERS pointers{&record, &context};
        report_crash(&pointers);
    }
    TerminateProcess(GetCurrentProcess(), 3);
    std::_Exit(3);
}

// The last filter: an exception no frame handled, of any kind. A C++ exception that leaves a thread through C or
// system frames reaches no terminate handler, so without this filter the process would end with 0xE06D7363 and no
// line or dump.
LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* info) {
    if (gCrashing.exchange(1) != 0) {
        // Another thread's fault is being written: this one waits for the dump and the report (the process ends
        // after them), so the first fault's dump is whole. Passed on, the system would end the process mid-write (an
        // empty dump). The writer's own fault is passed on: nothing would be left to wait for.
        if (GetCurrentThreadId() != gCrashWriterThread.load() && gCrashDone != nullptr) {
            char line[96];
            std::snprintf(line, sizeof(line), "another fault 0x%08lX (thread %lu) waits for the dump",
                          info->ExceptionRecord->ExceptionCode, GetCurrentThreadId());
            log_line("crash:", line);
            WaitForSingleObject(gCrashDone, 60000);
            Sleep(INFINITE);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    HMODULE owner = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress), &owner);
    // The module the fault is in and its offset, so a line names the place without the dump (a system module's
    // fault reads as d3d12core.dll+0x1810, not as a bare address that moves every start).
    char place[MAX_PATH + 32] = "";
    if (owner != nullptr) {
        wchar_t module[MAX_PATH];
        const DWORD n = GetModuleFileNameW(owner, module, MAX_PATH);
        const wchar_t* name = module;
        for (DWORD i = 0; i < n; i++) {
            if (module[i] == L'\\' || module[i] == L'/') {
                name = module + i + 1;
            }
        }
        std::snprintf(place, sizeof(place), " (%ls+0x%llX)", n != 0 ? name : L"?",
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress) -
                                                      reinterpret_cast<uintptr_t>(owner)));
    }
    char text[512];
    const bool cpp = info->ExceptionRecord->ExceptionCode == 0xE06D7363u;
    if (cpp) {
        describe_cpp_exception(info->ExceptionRecord, text, sizeof(text));
    } else {
        std::snprintf(text, sizeof(text), "unhandled exception 0x%08lX at 0x%p%s", info->ExceptionRecord->ExceptionCode,
                      info->ExceptionRecord->ExceptionAddress, place);
    }
    char line[640];
    std::snprintf(line, sizeof(line), "%s (thread %lu)", text, GetCurrentThreadId());
    log_line("crash:", line);
    gather_crash_facts(cpp ? galaxy::crash::Facts::Kind::Exception : galaxy::crash::Facts::Kind::Fault, line,
                       !cpp && owner != nullptr && owner == static_cast<HMODULE>(gModule.module),
                       info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW);
    if (!cpp) {
        note_crash_plugin(info);
    }
    gCrashForeign = !cpp && owner != GetModuleHandleW(nullptr) && owner != static_cast<HMODULE>(gModule.module);
    report_crash(info);
    TerminateProcess(GetCurrentProcess(), 4);
    std::_Exit(4);
}

#if defined(NEBULA_HOST_RENDERER)
// Nebula --crash-report <file>: the report of an instance that stopped (report_crash_facts), shown by this new
// process. The technical details stay in the file, which the dialog offers to show.
// The language folder whose words the crash dialog uses: the player's setting, for Auto (or settings that do not load,
// the reason for some reports) the system's.
const char* crash_dialog_language() {
    galaxy::GameLanguage language = galaxy::GameLanguage::Auto;
    try {
        galaxy::load_runtime_settings_from_file();
        language = galaxy::get_runtime_settings().language;
    } catch (const std::exception&) {
    }
    const LANGID system = GetUserDefaultUILanguage();
    const auto region = [system](WORD sublanguage) { return SUBLANGID(system) == sublanguage; };
    const bool spain = PRIMARYLANGID(system) == LANG_SPANISH && region(SUBLANG_SPANISH_MODERN);
    const bool canada = PRIMARYLANGID(system) == LANG_FRENCH && region(SUBLANG_FRENCH_CANADIAN);
    switch (language) {
    case galaxy::GameLanguage::Japanese: return "JpJapanese";
    case galaxy::GameLanguage::English: return "UsEnglish";
    case galaxy::GameLanguage::German: return "EuGerman";
    case galaxy::GameLanguage::French: return canada ? "UsFrench" : "EuFrench";
    case galaxy::GameLanguage::Spanish: return spain ? "EuSpanish" : "UsSpanish";
    case galaxy::GameLanguage::Italian: return "EuItalian";
    case galaxy::GameLanguage::Dutch: return "EuDutch";
    case galaxy::GameLanguage::Auto: break;
    }
    switch (PRIMARYLANGID(system)) {
    case LANG_JAPANESE: return "JpJapanese";
    case LANG_GERMAN: return "EuGerman";
    case LANG_FRENCH: return canada ? "UsFrench" : "EuFrench";
    case LANG_SPANISH: return spain ? "EuSpanish" : "UsSpanish";
    case LANG_ITALIAN: return "EuItalian";
    case LANG_DUTCH: return "EuDutch";
    case LANG_PORTUGUESE: return "UsPortuguese";
    case LANG_KOREAN: return "KrKorean";
    case LANG_CHINESE:
        return region(SUBLANG_CHINESE_SIMPLIFIED) || region(SUBLANG_CHINESE_SINGAPORE) ? "CnSimpChinese" : "TwTradChinese";
    default: return "UsEnglish";
    }
}

std::wstring wide_of(std::string_view text) {
    std::wstring wide(static_cast<std::size_t>(
                          MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0)),
                      L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), static_cast<int>(wide.size()));
    return wide;
}

int show_crash_report_dialog() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (args == nullptr) return 1;
    const fs::path file = count >= 3 ? fs::path(args[2]) : fs::path();
    LocalFree(args);
    std::string text;
    if (FILE* in = file.empty() ? nullptr : _wfopen(file.c_str(), L"rb")) {
        char buffer[4096];
        std::size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof(buffer), in)) > 0) text.append(buffer, n);
        std::fclose(in);
    }
    if (text.empty()) return 1;
    if (const std::size_t technical = text.find("\nTechnical details:"); technical != std::string::npos) {
        text.resize(technical);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    const char* language = crash_dialog_language();
    text = galaxy::crash::translated_report(text, language);
    text += "\n\n";
    text += galaxy::crash::translated("Open the folder with the report?", language);
    const std::wstring wide = wide_of(text);
    const std::wstring caption = wide_of(galaxy::crash::translated("Nebula stopped", language));
    if (MessageBoxW(nullptr, wide.c_str(), caption.c_str(), MB_ICONERROR | MB_YESNO | MB_SETFOREGROUND | MB_TOPMOST) ==
        IDYES) {
        const std::wstring select = L"/select,\"" + file.wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", select.c_str(), nullptr, SW_SHOWNORMAL);
    }
    return 0;
}
#endif

void install_crash_reporter() {
    gCrashRequest = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    gCrashDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
#if defined(NEBULA_HOST_RENDERER)
    galaxy::crash_watch::start(crash_folder(), kStart, galaxy::render::os_hdr::list_path());
#else
    galaxy::crash_watch::start(crash_folder(), kStart);
#endif
    std::thread(crash_writer).detach();
    AddVectoredExceptionHandler(0, crash_handler);  // last: the module's own handlers (division, stack guards) first
    SetUnhandledExceptionFilter(unhandled_filter);  // whatever no frame handled, C++ exceptions included
    std::signal(SIGABRT, abort_handler);            // process-wide in the UCRT, unlike set_terminate
}
#endif

#if defined(_WIN32)
// The stack that called a fatal, as module + offset (no folder names).
void log_fatal_stack() {
    void* frames[32];
    const USHORT count = RtlCaptureStackBackTrace(1, 32, frames, nullptr);
    for (USHORT i = 0; i < count; i++) {
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<const char*>(frames[i]), &module)) {
            K32GetModuleBaseNameA(GetCurrentProcess(), module, name, MAX_PATH);
        }
        char line[MAX_PATH + 32];
        std::snprintf(line, sizeof(line), "%s+0x%llx", name,
                      static_cast<unsigned long long>(static_cast<const char*>(frames[i]) - reinterpret_cast<const char*>(module)));
        log_line("fatal stack:", line);
    }
}
#endif

void svc_fatal(void*, const char* text) {
    log_line("fatal:", text);
#if defined(_WIN32)
    log_fatal_stack();
#endif
    gather_crash_facts(galaxy::crash::Facts::Kind::GameFatal, text);
#if defined(_WIN32)
    if (gCrashing.exchange(1) == 0) {
        report_crash(nullptr);
    }
#else
    report_crash_facts({});
#endif
    std::fflush(stdout);
    std::_Exit(3);
}

#if defined(_WIN32)
// Arguments a restart adds to this start's command line (--gpu-start-retry).
std::wstring gRestartArguments;
#else
// main's arguments as given: a restart (finish_and_exit) starts with them again.
char** gArgv = nullptr;
#if defined(NEBULA_HOST_RENDERER) && (defined(__linux__) || (defined(__APPLE__) && TARGET_OS_OSX)) && !defined(__ANDROID__)
#define NEBULA_HOST_START_AGAIN 1
// Arguments a restart adds after main's (read in order: a later value wins).
std::vector<std::string> gRestartExtra;
#endif
#endif

#if defined(NEBULA_HOST_RENDERER) && defined(NEBULA_DIAG_REPORTS)
// The settings menu's Send to developer (ui/benchmark_menu.h): the result's report, sealed and sent once, on a thread of
// its own; the menu shows how it ended.
void send_benchmark_report(const fs::path& result) {
    std::thread([result] {
        const galaxy::diag::SendResult sent = galaxy::diag::send_report_file(result, false);
        log_line("diag:", sent.line.c_str());
        using galaxy::ui::benchmark_menu::SendOutcome;
        SendOutcome outcome = SendOutcome::NotMade;
        if (sent.exit == galaxy::diag::kOk || sent.exit == galaxy::diag::kAlreadySent) {
            outcome = SendOutcome::Sent;
        } else if (sent.exit == galaxy::diag::kNetwork) {
            outcome = SendOutcome::NoConnection;
        } else if (sent.exit == galaxy::diag::kRefused) {
            outcome = sent.status == 429 ? SendOutcome::TooMany
                      : sent.status == 503 ? SendOutcome::ServerBusy
                                           : SendOutcome::Refused;
        }
        galaxy::ui::benchmark_menu::report_sent(outcome, sent.line);
    }).detach();
}

#if defined(_WIN32) || defined(NEBULA_HOST_START_AGAIN)
#define NEBULA_HOST_SHOW_REPORT 1
// A file in the desktop's own viewer (the file kind's app, open, xdg-open), in a process of its own that this one does
// not wait for.
void show_file(const fs::path& file) {
#if defined(_WIN32)
    ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
#if defined(__APPLE__)
    const char* const tool = "/usr/bin/open";
#else
    const char* const tool = "xdg-open";
#endif
    std::string path = file.string();
    std::string name = tool;
    char* const argv[] = {name.data(), path.data(), nullptr};
    const pid_t child = fork();
    if (child == 0) {
        if (fork() == 0) {  // the viewer's opener, adopted by init: no child of the game is left behind
            for (int fd = 3; fd < 1024; ++fd) close(fd);
            execvp(tool, argv);
        }
        _exit(0);
    }
    if (child > 0) (void)waitpid(child, nullptr, 0);
#endif
}

// The send question's What is sent: exactly what a send seals, written beside the result and opened in the viewer.
void show_benchmark_report(const fs::path& result) {
    std::string report;
    const galaxy::diag::SendResult preview = galaxy::diag::report_preview(result, false, report);
    fs::path shown = result;
    shown += ".report.json";
    if (preview.exit == galaxy::diag::kOk && galaxy::io::write_file_atomically(shown, report)) {
        show_file(shown);
    } else {
        log_line("diag:", preview.line.c_str());
    }
}
#endif
#endif

// An argument a restart adds (each a word of its own).
[[maybe_unused]] void add_restart_argument(const fs::path& argument) {
#if defined(_WIN32)
    gRestartArguments += L" \"" + argument.wstring() + L"\"";
#elif defined(NEBULA_HOST_START_AGAIN)
    gRestartExtra.push_back(argument.string());
#else
    (void)argument;
#endif
}

#if defined(NEBULA_HOST_RENDERER)
// The renderer: the game's records reach it in frames (render_frame at the game's frame end and, while the start runs
// ahead, at the frame, which also presents). All of this runs on the simulation thread, where the game makes these
// calls.
std::atomic<uint64_t> gSubmissions{0};  // render_frame calls with commands: the renderer's frame index

galaxy::host::NativeGameAudio* gAudio = nullptr;

void submit_frame_unchecked(bool present, bool black) {
    galaxy::render::render_frame(present, black);
    gSubmissions.fetch_add(1, std::memory_order_relaxed);
}

[[noreturn]] void finish_and_exit(bool restart);
// The start a restart asks for (host_window.h Restart): Android's activity reads it (start_again); the other hosts add
// its arguments (add_restart_argument) before finish_and_exit.
int gRestartKind = 1;

// The card has no memory free at the start: another program holds it for a moment. A new process tries again
// (--gpu-start-retry counts the tries), up to five times, two seconds apart; after that it is a fault to report.
bool retry_start_for_gpu_memory(const std::string& reason) {
#if defined(_WIN32)
    const unsigned count = ::galaxy::launch_args().gpu_start_retry;
    char out_of_memory[16];
    std::snprintf(out_of_memory, sizeof(out_of_memory), "0x%08lX", static_cast<unsigned long>(E_OUTOFMEMORY));
    const bool wanted = reason.find(out_of_memory) != std::string::npos || reason.find("VK_ERROR_OUT_OF") != std::string::npos ||
                        reason.find("VkResult -1") != std::string::npos || reason.find("VkResult -2") != std::string::npos;
    if (!wanted || count >= 5u) {
        return false;
    }
    gRestartArguments = L" --gpu-start-retry " + std::to_wstring(count + 1u);
    log_line("host: no graphics memory free at the start, trying again:", reason.c_str());
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return true;
#else
    (void)reason;
    return false;
#endif
}

// The renderer's hard failures end the game with their line and a dump. Caught here: thrown on the game's stacks they
// could not unwind through its frames.
void submit_frame(bool present, bool black) {
    try {
        submit_frame_unchecked(present, black);
    } catch (const std::exception& error) {
        const std::string text = std::string("the renderer stopped: ") + error.what();
        svc_fatal(nullptr, text.c_str());
    }
}
#endif

// The stage the game reports before each 3D scene (game_stage): a crash report names it.

void svc_game_stage(void*, const char* name) {
    if (name != nullptr && gSceneStage != name) {
        gSceneStage = name;
#if defined(NEBULA_HOST_RENDERER)
        if (::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs)) {
            const std::uint32_t hash = galaxy::render::rt_stage_hash(gSceneStage);
            std::fprintf(stderr, "[rt-inputs] stage %s: %08x, %s\n", name, hash,
                         galaxy::render::rt_stage(hash) != nullptr ? "a row of its own" : "the defaults");
        }
#endif
    }
}

// Online co-op (docs/COOP_PLAN.md): the local player as the game last reported it (coop_player), for the netcode to
// send; a change of instance (stage or scenario) is logged.
std::mutex gCoopLock;
NebulaCoopPlayer gCoopLocal{};
bool gCoopLocalValid = false;

void svc_coop_player(void*, const NebulaCoopPlayer* player) {
    if (player == nullptr) {
        return;
    }
    NebulaCoopPlayer copy = *player;
    copy.stage[sizeof(copy.stage) - 1] = '\0';
    copy.animation[sizeof(copy.animation) - 1] = '\0';
    copy.group[sizeof(copy.group) - 1] = '\0';
    for (char* track : copy.tracks) track[sizeof(copy.tracks[0]) - 1] = '\0';
    bool moved = false;
    {
        const std::lock_guard<std::mutex> lock(gCoopLock);
        moved = !gCoopLocalValid || std::strcmp(gCoopLocal.stage, copy.stage) != 0 ||
                gCoopLocal.scenario != copy.scenario;
        gCoopLocal = copy;
        gCoopLocalValid = true;
    }
    if (moved) {
        std::fprintf(stderr, "[coop] instance %s, scenario %d\n", copy.stage, static_cast<int>(copy.scenario));
    }
}

// The Discord application Nebula's status shows under (Mackery's; its large image is the "smg" asset).
constexpr const char* kDiscordClientId = "1567615368938397866";

// The game's message text (UTF-16, its tags skipped: 0x1A, then the tag's size in bytes in the next unit's high byte)
// as UTF-8.
std::string message_utf8(const uint16_t* text) {
    std::string out;
    if (text == nullptr) return out;
    for (size_t i = 0; i < 256u && text[i] != 0u; ++i) {
        uint32_t c = text[i];
        if (c == 0x1Au) {
            const uint32_t units = (static_cast<uint32_t>(text[i + 1u]) >> 8u) / 2u;
            if (units < 2u) break;
            i += units - 1u;
            continue;
        }
        if (c >= 0xD800u && c <= 0xDBFFu && text[i + 1u] >= 0xDC00u && text[i + 1u] <= 0xDFFFu) {
            c = 0x10000u + ((c - 0xD800u) << 10u) + (text[i + 1u] - 0xDC00u);
            ++i;
        }
        if (c < 0x20u) c = ' ';
        if (c < 0x80u) {
            out += static_cast<char>(c);
        } else if (c < 0x800u) {
            out += static_cast<char>(0xC0u | (c >> 6u));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        } else if (c < 0x10000u) {
            out += static_cast<char>(0xE0u | (c >> 12u));
            out += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        } else {
            out += static_cast<char>(0xF0u | (c >> 18u));
            out += static_cast<char>(0x80u | ((c >> 12u) & 0x3Fu));
            out += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

#if defined(NEBULA_HOST_RENDERER)
std::atomic<bool> gInPlay{false};  // a file is in play on a stage (svc_presence)
std::atomic<bool> gTitleShown{false};  // the title has shown since the start (svc_presence)
// The result of a run in the game that ended (start_benchmark_here), opened over the title when it shows.
std::mutex gResultsMutex;
std::filesystem::path gResultsPending;
std::atomic<bool> gResultsWaiting{false};
std::atomic<uint32_t> gResultsFrame{0u};  // the game frame they open at (svc_frame_begun); 0 none
constexpr uint32_t kResultsAfterTitleFrames = 30u;
#endif

// What the player is doing, for Discord: the galaxy (or the file select) and the star, else the Power Stars.
void svc_presence(void*, const uint16_t* galaxy, const uint16_t* star, int32_t power_stars) {
#if defined(NEBULA_HOST_RENDERER)
    if (power_stars >= 0) {  // GameScene::start (the file select reports -1)
        galaxy::bench::scene_started();
        galaxy::calibration_scene::scene_started();
    }
    // The HDR calibration's reference stage is not the player's: neither play nor what Discord shows.
    if (galaxy::calibration_scene::live()) return;
    // A file is in play on a stage (the title's stage has no galaxy name): the benchmark's question says what it loses.
    const bool title = power_stars >= 0 && (galaxy == nullptr || galaxy[0] == 0);
    gInPlay.store(power_stars >= 0 && !title, std::memory_order_relaxed);
    if (title) gTitleShown.store(true, std::memory_order_relaxed);
    // The title after a run in the game: its results open over it once it shows (svc_frame_begun).
    if (title && gResultsWaiting.exchange(false, std::memory_order_acq_rel)) {
        gResultsFrame.store(std::max(gGameFrames.load(std::memory_order_relaxed) + kResultsAfterTitleFrames, 1u),
                            std::memory_order_release);
    }
#endif
    galaxy::discord::Activity activity;
    if (power_stars < 0) {
        activity.details = "Choosing a file";
    } else {
        activity.details = message_utf8(galaxy);
        activity.state = message_utf8(star);
        if (activity.details.empty()) {
            activity.details = "At the title screen";  // the title's stage has no galaxy name
            activity.state.clear();
            static bool reached = false;
            if (!reached) {
                reached = true;
                boot_mark("title screen");
            }
        } else if (activity.state.empty()) {
            activity.state = std::to_string(power_stars) + (power_stars == 1 ? " Power Star" : " Power Stars");
        }
    }
    static std::string last;
    const std::string line = activity.details + " | " + activity.state;
    if (line != last) {
        last = line;
        log_line("presence:", line.c_str());
    }
    galaxy::discord::set_activity(activity);
}

void report_coop_achievement(const char* id);

// An achievement the save data meets: unlocked once (achievements.ini), with its English name, and in a room the
// save's for every player in it. The game names it in the played language itself (NebulaText). The headless host
// keeps no achievements.
int svc_achievement(void*, const char* id, uint16_t* name, uint32_t capacity) {
#if !defined(NEBULA_HOST_RENDERER)
    (void)id, (void)name, (void)capacity;
    return 0;
#else
    // A benchmark and the HDR calibration's reference stage unlock nothing.
    if (galaxy::bench::running() || galaxy::calibration_scene::live()) return 0;
    const galaxy::achievements::Info* info = id != nullptr ? galaxy::achievements::find(id) : nullptr;
    if (info == nullptr || !galaxy::achievements::unlock(id)) return 0;
    uint32_t i = 0;
    for (; name != nullptr && i + 1u < capacity && info->name[i] != '\0'; ++i) {
        name[i] = static_cast<unsigned char>(info->name[i]);
    }
    if (name != nullptr && capacity != 0u) name[i] = 0u;
    log_line("achievement:", info->name);
    report_coop_achievement(id);
    return 1;
#endif
}

int32_t svc_achievement_date(void*, const char* id) {
#if !defined(NEBULA_HOST_RENDERER)
    (void)id;
    return 0;
#else
    return id != nullptr ? galaxy::achievements::unlocked_date(id) : 0;
#endif
}

// The running mods' achievements (galaxy::mods). The headless host shows none.
int32_t svc_mod_achievement_count(void*) {
#if !defined(NEBULA_HOST_RENDERER)
    return 0;
#else
    return static_cast<int32_t>(std::min<std::size_t>(galaxy::mods::mod_achievement_count(), 1024u));
#endif
}

int32_t svc_mod_achievement(void*, int32_t index, const char** key, const char** name, const char** description, const char** mod,
                            int32_t* hidden) {
    const galaxy::mods::AchievementToAdd* achievement =
        index >= 0 && index < svc_mod_achievement_count(nullptr) ? galaxy::mods::mod_achievement(static_cast<std::size_t>(index)) : nullptr;
    if (achievement == nullptr) return 0;
    if (key != nullptr) *key = achievement->key.c_str();
    if (name != nullptr) *name = achievement->name.c_str();
    if (description != nullptr) *description = achievement->description.c_str();
    if (mod != nullptr) *mod = achievement->mod_name.c_str();
    if (hidden != nullptr) *hidden = achievement->hidden ? 1 : 0;
    return 1;
}

int32_t svc_mod_achievement_unlocked(void*) {
#if !defined(NEBULA_HOST_RENDERER)
    return -1;
#else
    const std::int32_t index = galaxy::mods::next_mod_achievement_unlocked();
    if (index >= 0) {
        if (const galaxy::mods::AchievementToAdd* achievement = galaxy::mods::mod_achievement(static_cast<std::size_t>(index))) {
            log_line("achievement:", achievement->key.c_str());
        }
    }
    return index;
#endif
}

// The all-time counters the achievement board shows (stats.ini). The headless host keeps none.
void svc_stat_add(void*, const char* name, uint64_t delta) {
#if defined(NEBULA_HOST_RENDERER)
    // A benchmark and the HDR calibration's reference stage count nothing.
    if (name != nullptr && !galaxy::bench::running() && !galaxy::calibration_scene::live()) galaxy::stats::add(name, delta);
#else
    (void)name, (void)delta;
#endif
}

uint64_t svc_stat_get(void*, const char* name) {
#if defined(NEBULA_HOST_RENDERER)
    return name != nullptr ? galaxy::stats::get(name) : 0u;
#else
    (void)name;
    return 0u;
#endif
}

// The settings view as the game was last given it. The host owns the strings, the page names and the rows that
// NebulaSettingsView points at; they stay as they are until the content changes, and the revision counts the changes.
struct SettingsRowContent {
    std::string label;
    std::string value;
    uint32_t flags = 0;
    uint16_t slider_steps = 0;
    uint16_t slider_at = 0;
    bool operator==(const SettingsRowContent&) const = default;
};

struct SettingsContent {
    uint32_t open = 0;
    uint32_t page = 0;
    uint32_t controls_page = 0;
    uint32_t input = 0;
    uint32_t selected = 0;
    uint32_t selected_button = 0;
    uint32_t mod_list_count[2] = {0, 0};
    uint32_t status = 0;
    int32_t status_number = 0;
    std::vector<std::string> pages;
    std::vector<SettingsRowContent> rows;
    std::string help;
    uint32_t help_as_is = 0;
    std::string status_text;
    std::string long_text;
    uint32_t kind = NEBULA_SETTINGS_KIND_SETTINGS;
    uint32_t back = 0;
    uint32_t editing = 0;
    // Held here while the view points at its pixels; the same pointer is the same picture.
    std::shared_ptr<const galaxy::render::NativeSettingsPicture> picture;
    galaxy::render::NativeSettingsGraph graph;
    uint32_t scene_live = 0;
    bool operator==(const SettingsContent&) const = default;
};

struct SettingsViewStorage {
    SettingsContent content;
    bool built = false;
    uint32_t revision = 0;
    // What the view points at, made from the content whenever it changes.
    std::vector<const char*> page_names;
    std::vector<NebulaSettingsRow> rows;
};

SettingsViewStorage g_settings_view;

// The view from `next`: the held content and what the view points at are made again only when it changed.
void publish_settings_view(SettingsContent next, NebulaSettingsView* view) {
    SettingsViewStorage& held = g_settings_view;
    if (!held.built || !(next == held.content)) {
        held.content = std::move(next);
        held.built = true;
        ++held.revision;
        held.page_names.clear();
        for (const std::string& name : held.content.pages) held.page_names.push_back(name.c_str());
        held.rows.clear();
        for (const SettingsRowContent& row : held.content.rows) {
            held.rows.push_back(NebulaSettingsRow{row.label.c_str(), row.value.c_str(), row.flags, row.slider_steps, row.slider_at});
        }
    }
    const SettingsContent& content = held.content;
    view->open = content.open;
    view->page = content.page;
    view->page_count = static_cast<uint32_t>(held.page_names.size());
    view->pages = held.page_names.data();
    view->controls_page = content.controls_page;
    view->input = content.input;
    view->revision = held.revision;
    view->total_rows = static_cast<uint32_t>(held.rows.size());
    view->rows = held.rows.data();
    view->selected = content.selected;
    view->selected_button = content.selected_button;
    view->mod_list_count[0] = content.mod_list_count[0];
    view->mod_list_count[1] = content.mod_list_count[1];
    view->help = content.help.c_str();
    view->help_as_is = content.help_as_is;
    view->status = content.status;
    view->status_number = content.status_number;
    view->status_text = content.status_text.c_str();
    view->long_text = content.long_text.c_str();
    view->scene_live = content.scene_live;
    view->kind = content.kind;
    view->back = content.back;
    view->editing = content.editing;
    if (content.picture != nullptr && !content.picture->rgba.empty()) {
        view->picture = content.picture->rgba.data();
        view->picture_width = content.picture->width;
        view->picture_height = content.picture->height;
        const galaxy::render::NativeSettingsGraph& graph = content.graph;
        if (graph.right > graph.left && graph.bottom > graph.top && graph.right <= content.picture->width &&
            graph.bottom <= content.picture->height && graph.top_fps > 0.0f) {
            view->graph_left = graph.left;
            view->graph_top = graph.top;
            view->graph_right = graph.right;
            view->graph_bottom = graph.bottom;
            static_assert(galaxy::ui::kBenchmarkResultsSendable == NEBULA_BENCHMARK_RESULTS_SENDABLE &&
                          galaxy::ui::kBenchmarkResultsSent == NEBULA_BENCHMARK_RESULTS_SENT &&
                          galaxy::ui::kBenchmarkFromResults == NEBULA_BENCHMARK_FROM_RESULTS);
            using galaxy::ui::benchmark_menu::SendOutcome;
            static_assert(static_cast<int>(SendOutcome::NoConnection) == NEBULA_BENCHMARK_SEND_NO_CONNECTION &&
                          static_cast<int>(SendOutcome::TooMany) == NEBULA_BENCHMARK_SEND_TOO_MANY &&
                          static_cast<int>(SendOutcome::ServerBusy) == NEBULA_BENCHMARK_SEND_SERVER_BUSY &&
                          static_cast<int>(SendOutcome::Refused) == NEBULA_BENCHMARK_SEND_REFUSED &&
                          static_cast<int>(SendOutcome::NotMade) == NEBULA_BENCHMARK_SEND_NOT_MADE);
            view->graph_top_fps = graph.top_fps;
            view->graph_step_fps = graph.step_fps;
            view->graph_stage_count = static_cast<uint32_t>(graph.stage_starts.size());
            view->graph_stage_starts = graph.stage_starts.empty() ? nullptr : graph.stage_starts.data();
        }
    }
}
// The game's reads of the menu's view in a row that found it open (svc_settings_view; 0 once one finds it closed): at 1
// the game has not yet drawn it, so a close then is the game's, not a press on it (svc_settings_input).
std::uint32_t g_settings_open_views = 0;

#if defined(NEBULA_HOST_RENDERER)
static_assert(galaxy::kGameStepsPerSecond == NEBULA_STEPS_PER_SECOND, "the game and the host step at one rate");
static_assert(static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::Preparing) == NEBULA_SETTINGS_STATUS_PREPARING &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::Arrange) == NEBULA_SETTINGS_STATUS_ARRANGE &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::ModAbout) == NEBULA_SETTINGS_STATUS_MOD_ABOUT &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::ModRemoveAsk) == NEBULA_SETTINGS_STATUS_MOD_REMOVE_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::ModForgetAsk) == NEBULA_SETTINGS_STATUS_MOD_FORGET_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::BenchmarkAsk) == NEBULA_SETTINGS_STATUS_BENCHMARK_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::BenchmarkResults) ==
                  NEBULA_SETTINGS_STATUS_BENCHMARK_RESULTS &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::BenchmarkSendAsk) ==
                  NEBULA_SETTINGS_STATUS_BENCHMARK_SEND_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::RendererRestart) ==
                  NEBULA_SETTINGS_STATUS_RENDERER_RESTART &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::BenchmarkSending) ==
                  NEBULA_SETTINGS_STATUS_BENCHMARK_SENDING &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::BenchmarkSent) ==
                  NEBULA_SETTINGS_STATUS_BENCHMARK_SENT &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::BenchmarkSendFailed) ==
                  NEBULA_SETTINGS_STATUS_BENCHMARK_SEND_FAILED &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::HdrCalibration) ==
                  NEBULA_SETTINGS_STATUS_HDR_CALIBRATION &&
              static_cast<unsigned>(galaxy::ui::hdr_calibration::Step::Peak) == NEBULA_HDR_STEP_PEAK &&
              static_cast<unsigned>(galaxy::ui::hdr_calibration::Step::Black) == NEBULA_HDR_STEP_BLACK &&
              static_cast<unsigned>(galaxy::ui::hdr_calibration::Step::PaperWhite) == NEBULA_HDR_STEP_PAPER_WHITE &&
              galaxy::ui::hdr_calibration::kStepMask == NEBULA_HDR_STEP_MASK &&
              galaxy::ui::hdr_calibration::kPlaceShift == NEBULA_HDR_PLACE_SHIFT &&
              galaxy::ui::hdr_calibration::kLastShift == NEBULA_HDR_LAST_SHIFT &&
              galaxy::ui::hdr_calibration::kFieldMask == NEBULA_HDR_FIELD_MASK &&
              galaxy::ui::hdr_calibration::kSceneWaitBit == NEBULA_HDR_SCENE_WAIT &&
              galaxy::render::kHdrBoxSide == NEBULA_HDR_BOX_SIDE &&
              galaxy::render::kHdrBoxCentreY == NEBULA_HDR_BOX_CENTRE_Y &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::Leave) == NEBULA_SETTINGS_STATUS_LEAVE &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::ModsRestartAsk) == NEBULA_SETTINGS_STATUS_MODS_RESTART_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::KeepDisplay) == NEBULA_SETTINGS_STATUS_KEEP_DISPLAY &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::JoyCon) == NEBULA_SETTINGS_STATUS_JOYCON &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::RtRestartAsk) ==
                  NEBULA_SETTINGS_STATUS_RT_RESTART_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::DiagnosticsAsk) ==
                  NEBULA_SETTINGS_STATUS_DIAGNOSTICS_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::ModsCodeAsk) == NEBULA_SETTINGS_STATUS_MODS_CODE_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::XessAsk) == NEBULA_SETTINGS_STATUS_XESS_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::License) == NEBULA_SETTINGS_STATUS_LICENSE &&
              galaxy::render::kJoyConLeftBit == NEBULA_JOYCON_LEFT && galaxy::render::kJoyConRightBit == NEBULA_JOYCON_RIGHT &&
              galaxy::render::kJoyConLeftHeldBit == NEBULA_JOYCON_LEFT_HELD &&
              galaxy::render::kJoyConRightHeldBit == NEBULA_JOYCON_RIGHT_HELD &&
              galaxy::render::kJoyConPairedBit == NEBULA_JOYCON_PAIRED &&
              galaxy::render::kJoyConLeftPressesShift == NEBULA_JOYCON_LEFT_PRESSES_SHIFT &&
              galaxy::render::kJoyConRightPressesShift == NEBULA_JOYCON_RIGHT_PRESSES_SHIFT,
              "the settings statuses are numbered alike");
static_assert(static_cast<uint32_t>(galaxy::render::NativeSettingsInput::Open) == NEBULA_SETTINGS_OPEN &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsInput::Revert) == NEBULA_SETTINGS_REVERT &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsInput::MoveMod) == NEBULA_SETTINGS_MOVE_MOD &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsInput::SetValue) == NEBULA_SETTINGS_SET_VALUE,
              "the settings inputs are numbered alike");
#endif

// The settings menu for the game's own (NebulaSettingsMenu): the overlay's state, which the game draws from then on.
// The headless host has no settings menu: always closed.
// The online co-op main menu (coop_menu), in place of the title and the file select. Its Options opens Nebula's
// settings menu where this host has one (every renderer's: the Windows window's, and SDL's on Linux and Android); the
// main menu waits under it.
#if defined(NEBULA_HOST_RENDERER)
constexpr bool kHostSettingsMenu = true;
#else
constexpr bool kHostSettingsMenu = false;
#endif
galaxy::coop::MainMenu gCoopMenu{kHostSettingsMenu};
std::vector<galaxy::coop::RoomMod> coop_room_mods();
// The co-op saves, under the NAND root (coop_saves/): made once the NAND root is known.
std::optional<galaxy::coop::SaveStore> gCoopSaves;
unsigned gCoopSaveFile = 1u;  // the user file the played save plays

// The character the player wants (NEBULA_COOP_MENU_CHARACTER): Options' PLAYER page's, as Host or Join left the menu,
// then as the page changes in game (renderer builds). The game plays it from its next stage start, and a switch in
// play reloads the player's spot as the other character (NebulaCharacter).
std::atomic<bool> gCoopLuigi{false};
// And its id (coop_character_wanted): a mod's character on that base, else the base's own ("mario", "luigi").
std::mutex gCoopCharacterLock;
std::string gCoopCharacterId = "mario";
// And the colours (coop_palette_wanted): Options', or none for the character's own (CUSTOM keeps its colours written
// out even when they are the character's own; worn, they are none, as OWN).
std::optional<galaxy::CoopPalette> gCoopPalette;

void set_coop_character_wanted(const galaxy::RuntimeCoopSettings& coop) {
    gCoopLuigi.store(coop.character == galaxy::CoopCharacter::Luigi, std::memory_order_relaxed);
    std::optional<galaxy::CoopPalette> palette = galaxy::parse_coop_palette(coop.palette);
    if (palette && *palette == galaxy::own_palette(coop)) palette.reset();
    const std::lock_guard<std::mutex> lock(gCoopCharacterLock);
    gCoopCharacterId = galaxy::coop_character_id(coop);
    gCoopPalette = palette;
}

// The room's chat (galaxy/coop_chat.h), over the session below (which goes first, its thread giving the chat lines),
// and its commands (galaxy/coop_commands.h).
galaxy::coop::Chat gCoopChat;
// The pop-ups of who joined, left, went where, died or got a star (galaxy/coop_popups.h), for the game's corner boxes
// (coop_popup_take).
galaxy::coop::Popups gCoopPopups;
// Online play (galaxy/coop_session.h): the room this player hosts or joined. Host opens it as its save starts; Join
// connects from the main menu and plays the host's save once let in; the pause menu's Quit (back at the main menu)
// leaves it.
galaxy::coop::Session gCoopSession;

#if defined(_WIN32)
void leave_coop_room_at_crash() {
    // Never waits: the thread that crashed may be inside the session.
    if (!gCoopSession.leave_if_free()) log_line("crash:", "the co-op room could not be told (the session was busy)");
}
#endif
void keep_coop_settings();
void keep_coop_list(bool ban, const std::string& identity);
// The game as online play compares it: a player with another is refused, as is one with another disc
// (gCoopDisc); one playing other mods gets the room's (galaxy/coop_mods.h).
// The co-op fingerprint (cmake/nebula_fingerprint.cmake) stands for the version: builds whose online play differs in
// any way refuse each other, and no change bumps a number by hand.
const std::string kCoopGameVersion = "nebula-coop-" NEBULA_COOP_FINGERPRINT;
galaxy::coop::DiscInfo gCoopDisc;
// This start played a room's mods for a join (the join Nebula started again for): the room's list.
std::optional<std::vector<galaxy::coop::SharedMod>> gCoopStartedRoomMods;
// Why the room ended, shown on the main menu as it opens again.
std::mutex gCoopNoticeLock;
std::string gCoopNotice;
// The command line's --host <port> or --join <address>: done once, as the main menu first opens.
enum class CoopAutoStart { None, Host, Join };
CoopAutoStart gCoopAutoStart = CoopAutoStart::None;
std::uint16_t gCoopAutoPort = 0u;
std::string gCoopAutoAddress;
// A Join pressed in Discord mid-game: the room's password, taken as the main menu opens and joins.
std::optional<std::string> gCoopAutoPassword;

// The other players' snapshots (galaxy/coop_snapshot.h), for their copies in this player's instance; and this
// player's, sent up to 30 times a second (10 while it stands still).
galaxy::coop::RemotePlayers gCoopRemote;
std::uint32_t gCoopSnapshotSequence = 0u;
std::chrono::steady_clock::time_point gCoopSnapshotSent{};
NebulaCoopPlayer gCoopSnapshotLast{};  // the report last sent (send_coop_snapshot sends again early only when it changed)

// The shared world (galaxy/coop_world.h): the host's record of each instance and the session's save, which the game
// reports to and is told from (coop_world_report, coop_world_take).
galaxy::coop::World gCoopWorld;

// Achievements another player unlocked in the room's save, now this player's too, for the game's pop-ups
// (coop_achievement_take), in order.
std::deque<std::string> gCoopAchievementsWaiting;

// This player's game unlocked an achievement: in a room, the save's, so the host keeps it and tells every player.
void report_coop_achievement(const char* id) {
#if defined(NEBULA_HOST_RENDERER)
    const std::int64_t when = galaxy::achievements::unlocked_time(id);
    gCoopWorld.report_save({galaxy::coop::SaveKind::Achievement, id, 0, static_cast<std::uint32_t>(when)});
#else
    (void)id;
#endif
}

// The room's achievements as they come (each scene frame): one this player's save doesn't have yet is unlocked on the
// day it was unlocked in the room, and waits for its pop-up.
void take_coop_achievements() {
    for (const galaxy::coop::SaveChange& change : gCoopWorld.take_achievements()) {
#if defined(NEBULA_HOST_RENDERER)
        const galaxy::achievements::Info* info = galaxy::achievements::find(change.name);
        if (info == nullptr || !galaxy::achievements::unlock_at(change.name, change.value)) continue;
        log_line("achievement (the room's):", info->name);
        gCoopAchievementsWaiting.push_back(change.name);
#else
        (void)change;
#endif
    }
}

// The others' snapshots as they come (on the session's thread), and the world's messages. Set before hosting or
// joining.
void listen_coop_snapshots() {
    galaxy::coop::listen_snapshots(gCoopSession, gCoopRemote);
    gCoopWorld.attach(gCoopSession);
}

// Whether the game's report of this player changed from the last one sent (its animation's frame moving on by itself
// doesn't count: the others' copies play it on).
bool coop_player_changed(const NebulaCoopPlayer& now, const NebulaCoopPlayer& sent) {
    NebulaCoopPlayer same = now;
    same.animation_frame = sent.animation_frame;
    return std::memcmp(&same, &sent, sizeof(same)) != 0;
}

// This player's snapshot to the others in its instance (the game's last report of it): as sm64coopdx's
// network_update_player, when it changed, at most kSnapshotInterval apart, and otherwise kSnapshotIdleInterval apart.
void send_coop_snapshot() {
    const auto now = std::chrono::steady_clock::now();
    if (now - gCoopSnapshotSent < galaxy::coop::kSnapshotInterval) return;
    NebulaCoopPlayer local{};
    {
        const std::lock_guard<std::mutex> lock(gCoopLock);
        if (!gCoopLocalValid) return;
        local = gCoopLocal;
    }
    if (now - gCoopSnapshotSent < galaxy::coop::kSnapshotIdleInterval && !coop_player_changed(local, gCoopSnapshotLast)) {
        return;
    }
    gCoopSnapshotSent = now;
    gCoopSnapshotLast = local;
    galaxy::coop::PlayerSnapshot snapshot;
    snapshot.sequence = ++gCoopSnapshotSequence;
    snapshot.stage = local.stage;
    snapshot.scenario = local.scenario;
    for (std::size_t i = 0; i < 3u; ++i) {
        snapshot.position[i] = local.position[i];
        snapshot.rotation[i] = local.rotation[i];
        snapshot.front[i] = local.front[i];
        snapshot.up[i] = local.up[i];
        snapshot.gravity[i] = local.gravity[i];
        snapshot.velocity[i] = local.velocity[i];
    }
    snapshot.group = local.group;
    snapshot.animation = local.animation;
    snapshot.frame = local.animation_frame;
    snapshot.rate = local.animation_rate;
    for (std::size_t i = 0; i < snapshot.weights.size(); ++i) snapshot.weights[i] = local.track_weights[i];
    for (std::size_t i = 0; i < snapshot.tracks.size(); ++i) snapshot.tracks[i] = local.tracks[i];
    snapshot.health = local.health;
    snapshot.mode = local.mode;
    snapshot.flags = local.flags;
    snapshot.area = local.area;
    snapshot.platform = local.platform;
    for (std::size_t i = 0; i < 3u; ++i) {
        snapshot.platform_position[i] = local.platform_position[i];
        snapshot.platform_front[i] = local.platform_front[i];
        snapshot.platform_up[i] = local.platform_up[i];
    }
    (void)galaxy::coop::send_snapshot(gCoopSession, snapshot);
}

void set_coop_notice(std::string text) {
    const std::lock_guard<std::mutex> lock(gCoopNoticeLock);
    gCoopNotice = std::move(text);
}

std::string take_coop_notice() {
    const std::lock_guard<std::mutex> lock(gCoopNoticeLock);
    return std::exchange(gCoopNotice, {});
}

// The host's save as a joiner gets it: the played save folder's files (the game's GameData.bin and the rest, and the
// save's achievements), read as they join, and the user file it plays.
galaxy::coop::RoomSave coop_room_save() {
#if defined(NEBULA_HOST_RENDERER)
    // The achievements unlocked a moment ago, in the file too.
    galaxy::achievements::flush();
#endif
    galaxy::coop::RoomSave save;
    save.file = gCoopSaveFile;
    fs::path folder;
    {
        const std::lock_guard<std::mutex> lock(gCoopSaveMutex);
        folder = gCoopSaveFolder;
    }
    std::error_code error;
    if (folder.empty() || !fs::is_directory(folder, error)) return save;
    std::size_t total = 0u;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
        if (!entry.is_regular_file(error)) continue;
        const std::string name = entry.path().filename().string();
        // The save's name and when it was played stay the host's; its achievements are every player's.
        if (name == "save.ini") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        total += bytes.size();
        if (total > galaxy::coop::kMaxSaveBytes) break;
        save.files.push_back({name, std::move(bytes)});
    }
    return save;
}

// The host's save into this player's folder for it (coop_joined/ under the NAND root, emptied first), for the game
// to play. False when it can't be written.
bool write_joined_save(const galaxy::coop::RoomSave& save, fs::path& folder) {
    folder = gSaveRoot / "coop_joined";
    std::error_code error;
    fs::remove_all(folder, error);
    fs::create_directories(folder, error);
    if (error) return false;
    for (const galaxy::coop::SaveFile& file : save.files) {
        // A plain file name: nothing that leaves the folder.
        const fs::path name = fs::path(file.name).filename();
        if (name.empty() || name != fs::path(file.name) || name == "." || name == "..") continue;
        std::ofstream out(folder / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(file.bytes.data()), static_cast<std::streamsize>(file.bytes.size()));
        if (!out) return false;
    }
    return true;
}

// Where the room's mods download (galaxy::coop::ModCache): coop_mods/ beside the mods folder. Empty without one.
fs::path coop_mods_cache() {
    const fs::path mods = galaxy::mods::mods_folder();
    return mods.empty() ? fs::path{} : mods.parent_path() / "coop_mods";
}

// The join that waits for Nebula to start again with a room's mods: in the cache, or a second game's own folder
// (galaxy/game_instance.h), as the games share the cache but each restarts to join on its own.
fs::path coop_next_join_file() {
    const fs::path cache = coop_mods_cache();
    if (cache.empty()) return {};
    const fs::path& instance = galaxy::game_instance_folder();
    return instance.empty() ? cache / ".next_join" : instance / ".next_join";
}

// The mods this game started with that change it (the room's, for a host; what a joiner plays now), with their files'
// hashes: read once, as they stay until Nebula starts again.
const std::vector<galaxy::coop::SharedMod>& coop_session_mods() {
    static const std::vector<galaxy::coop::SharedMod> mods = [] {
        std::vector<galaxy::coop::SharedMod> out;
        for (const galaxy::mods::ModInfo& mod : galaxy::mods::session_mods().started_with) {
            if (!mod.enabled || !galaxy::mods::changes_game_files(mod)) continue;
            galaxy::coop::SharedMod shared;
            shared.id = mod.id;
            shared.folder = mod.path;
            shared.own_text = mod.own_text;
            for (const galaxy::mods::ModOption& option : mod.options) {
                shared.options.emplace_back(option.key, option.selected);
            }
            if (!galaxy::coop::describe_mod(shared)) {
                std::fprintf(stderr, "[coop] the mod %s could not be read to share\n", mod.id.c_str());
            }
            out.push_back(std::move(shared));
        }
        return out;
    }();
    return mods;
}

// A joiner's own copy of a room's mod: the mods folder's of its name, when it has the same files.
std::optional<fs::path> find_own_mod(const galaxy::coop::SharedMod& mod) {
    const fs::path folder = galaxy::mods::mods_folder();
    if (folder.empty()) return std::nullopt;
    galaxy::coop::SharedMod own = mod;
    own.folder = folder / fs::path(std::u8string(reinterpret_cast<const char8_t*>(mod.id.data()), mod.id.size()));
    std::error_code error;
    if (!fs::is_directory(own.folder, error) || !galaxy::coop::describe_mod(own) || own.hash != mod.hash) {
        return std::nullopt;
    }
    return own.folder;
}

// The room's mods are all here (downloaded or the player's own): Nebula starts again with them and joins again. False
// (and why on the menu) when it doesn't start again now.
bool restart_for_room_mods(const std::vector<galaxy::coop::SharedMod>& mods, const std::string& address) {
    if (gCoopStartedRoomMods && galaxy::coop::same_mods(*gCoopStartedRoomMods, mods)) {
        // Started with these already: they didn't apply as the host plays them.
        std::fprintf(stderr, "[coop] the room's mods (%s) did not apply\n", galaxy::coop::mods_text(mods).c_str());
        gCoopMenu.set_message("COULD NOT JOIN: THE ROOM'S MODS DID NOT APPLY (" + galaxy::coop::mods_text(mods) + ")");
        return false;
    }
    const fs::path file = coop_next_join_file();
    if (file.empty() || !galaxy::coop::write_next_join(file, {address, mods})) {
        gCoopMenu.set_message("COULD NOT JOIN: THE ROOM'S MODS COULD NOT BE SET UP");
        return false;
    }
    std::fprintf(stderr, "[coop] the room's mods are ready (%s): starting again to join %s\n",
                 galaxy::coop::mods_text(mods).c_str(), address.c_str());
    bool restarting = false;
#if defined(NEBULA_HOST_RENDERER)
    restarting = galaxy::render::request_restart();
#endif
    gCoopMenu.set_message(restarting ? "NEBULA STARTS AGAIN WITH THE ROOM'S MODS, THEN JOINS"
                                     : "THE ROOM'S MODS ARE READY: CLOSE NEBULA AND START IT AGAIN TO JOIN");
    return restarting;
}

// The player's name as the room shows it: Options' PLAYER page's, PLAYER (as the page shows it) when none was given.
std::string coop_player_name() {
    const std::string& name = gCoopMenu.room().player_name;
    return name.empty() ? std::string("PLAYER") : name;
}

std::optional<std::string> coop_join_code(const std::string& text);

// Discord's view of the room (galaxy/discord_presence.h): how many play of how many, and on NebulaNet a Join button
// carrying the room's code, as sm64coopdx's activity has. sm64coopdx's rule for the button: the host always offers it
// (with the room's password), a joiner only for a room without one; and only on the NebulaNet everyone uses, which a
// friend's Nebula reaches.
void note_discord_party() {
    galaxy::discord::Party party;
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state == galaxy::coop::SessionState::Hosting || state == galaxy::coop::SessionState::Joined) {
        const galaxy::RuntimeCoopSettings room = gCoopSession.room();
        const bool host = state == galaxy::coop::SessionState::Hosting;
        party.size = static_cast<unsigned>(std::max<std::size_t>(gCoopSession.players().size(), 1u));
        party.max = std::max(room.max_players, party.size);
        std::string code = gCoopSession.room_code();
        if (code.empty() && !host) code = coop_join_code(gCoopMenu.room().join_address).value_or(std::string());
        const std::string& server = gCoopMenu.room().nebulanet_server;
        const bool everyones_server = server.empty() || server == galaxy::coop::kNebulaNetServer;
        if (!code.empty()) {
            party.id = galaxy::discord::party_id(code);
            const std::string& password = host ? room.password : gCoopMenu.room().password;
            if (everyones_server && (host || password.empty())) {
                party.join_secret = galaxy::discord::join_secret(code, password);
            }
        }
    }
    galaxy::discord::set_party(party);
}

// The Online page's view of the room (how many play, the host's settings for a joiner).
void note_coop_room() {
    note_discord_party();
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::CoopRoomStatus status;
    status.players = static_cast<unsigned>(std::max<std::size_t>(gCoopSession.players().size(), 1u));
    status.joined = gCoopSession.state() == galaxy::coop::SessionState::Joined;
    if (status.joined) status.room = gCoopSession.room();
    // The code friends join with: the host's from NebulaNet, or the one this player joined by.
    status.code = gCoopSession.room_code();
    if (status.code.empty() && status.joined) {
        status.code = galaxy::coop::parse_room_code(gCoopMenu.room().join_address).value_or(std::string());
    }
    galaxy::render::set_coop_room_status(status);
#endif
}

// Where this player plays (the player list's galaxy and star), as the game last reported it: the session tells the
// others when it changes. Nowhere once back at the main menu.
void note_coop_place() {
    std::string stage;
    int scenario = 0;
    {
        const std::lock_guard<std::mutex> lock(gCoopLock);
        if (gCoopLocalValid) {
            stage = gCoopLocal.stage;
            scenario = gCoopLocal.scenario;
        }
    }
    gCoopSession.set_place(stage, scenario);
}

// The room's news since the last call, on the game's thread: logged and shown, and a room that ended under the player
// (its host quit, crashed or stopped answering, or this player was removed) takes them back to the main menu.
void handle_coop_session_events() {
    using Kind = galaxy::coop::SessionEvent::Kind;
    bool changed = false;
    for (const galaxy::coop::SessionEvent& event : gCoopSession.take_events()) {
        switch (event.kind) {
        case Kind::PlayerJoined:
            std::fprintf(stderr, "[coop] %s joined\n", event.name.c_str());
            gCoopChat.notice(galaxy::coop::ChatLine::Kind::Joined, event.name, galaxy::coop::chat_clock_ms());
            changed = true;
            break;
        case Kind::PlayerLeft:
            std::fprintf(stderr, "[coop] %s left%s%s\n", event.name.c_str(), event.text.empty() ? "" : ": ",
                         event.text.c_str());
            gCoopChat.notice(galaxy::coop::ChatLine::Kind::Left, event.name, galaxy::coop::chat_clock_ms());
            changed = true;
            break;
        case Kind::RoomChanged: changed = true; break;
        case Kind::RoomCode:
            std::fprintf(stderr, "[coop] NebulaNet room code %s\n", event.text.c_str());
            gCoopChat.say("CoopCmdRoomCode", event.text);
            changed = true;
            break;
        case Kind::NebulaNetLost:
            std::fprintf(stderr, "[coop] NebulaNet: %s\n", event.text.c_str());
            gCoopChat.say(event.text);
            break;
        case Kind::Moderator: gCoopChat.say("CoopCmdModeratorGranted"); break;
        case Kind::KeepBan:
        case Kind::KeepModerator: keep_coop_list(event.kind == Kind::KeepBan, event.text); break;
        case Kind::Closed:
            std::fprintf(stderr, "[coop] the room ended: %s\n", event.text.empty() ? "left" : event.text.c_str());
            gCoopChat.clear();
            gCoopPopups.clear();
            if (!event.text.empty()) {
                set_coop_notice(event.text);
#if defined(NEBULA_HOST_RENDERER)
                if (galaxy::render::coop_in_game()) galaxy::render::request_leave_game();
#endif
            }
            changed = true;
            break;
        default: break;
        }
    }
    if (changed) note_coop_room();
}

// What happened in the room since the last call, on the game's thread: logged, and a room that ended while playing
// takes the player back to the main menu. The host's room settings (the Online page's) and the player's name (Options'
// PLAYER page's) reach the others as they change.
void pump_coop_session() {
    note_coop_place();
    if (!gCoopSession.active()) {
        if (gCoopWorld.active()) gCoopWorld.stop();
        gCoopAchievementsWaiting.clear();
        gCoopPopups.clear();
        // The room may have just ended on the session's own thread (the host left, or stopped answering): the session
        // is idle already, but its Closed event still waits here to say why and take the player to the main menu.
        if (!gCoopMenu.joining()) handle_coop_session_events();
        return;
    }
    // A join connecting from the main menu: its events are poll_coop_join's (Joined above all, which plays the save).
    if (gCoopMenu.joining()) return;
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state == galaxy::coop::SessionState::Hosting || state == galaxy::coop::SessionState::Joined) {
        send_coop_snapshot();
        gCoopWorld.update();
        take_coop_achievements();
#if defined(NEBULA_HOST_RENDERER)
        gCoopPopups.set_options(galaxy::get_runtime_settings().coop_popups);
#endif
        gCoopPopups.update(gCoopSession.players(), galaxy::coop::chat_clock_ms());
    }
#if defined(NEBULA_HOST_RENDERER)
    const galaxy::RuntimeCoopSettings settings = galaxy::get_runtime_settings().coop;
    if (gCoopSession.is_host()) gCoopSession.set_room(settings);
    gCoopSession.set_name(settings.player_name);
#endif
    // The character the game plays now (its snapshot's, which changes as a switch reloads the player's spot), not
    // the one wanted: the others' nametags, list and chat follow the model they see.
    {
        bool valid = false, luigi = false, has_palette = false;
        char character[sizeof(gCoopLocal.character)] = {};
        galaxy::CoopPalette palette{};
        {
            const std::lock_guard<std::mutex> lock(gCoopLock);
            valid = gCoopLocalValid;
            luigi = (gCoopLocal.flags & NEBULA_COOP_PLAYER_LUIGI) != 0u;
            std::memcpy(character, gCoopLocal.character, sizeof(character));
            has_palette = gCoopLocal.has_palette != 0u;
            std::memcpy(palette.data(), gCoopLocal.palette, palette.size());
        }
        character[sizeof(character) - 1] = '\0';
        // With the colours they wear (sm64coopdx's palette), which travel with the character.
        if (valid) {
            gCoopSession.set_character(luigi ? galaxy::CoopCharacter::Luigi : galaxy::CoopCharacter::Mario, character,
                                       has_palette ? &palette : nullptr);
        }
    }
    handle_coop_session_events();
}

#if defined(NEBULA_HOST_RENDERER)
bool host_settings_open();

// The chat's line, each scene frame: T opens it while a room's game shows with no menu over it (the window takes the
// key, galaxy/render/galaxy_select.h), the keyboard's typing goes to it, and it closes as it is sent or dropped, or when
// a menu comes over the game.
void pump_coop_chat() {
    const bool available = gCoopSession.active() && galaxy::render::coop_in_game() && !host_settings_open() &&
                           !gCoopMenu.is_open();
    galaxy::render::set_coop_chat_available(available);
    // Whether the window's typing is the chat's (the main menu's text rows have it otherwise).
    static bool s_typing_here = false;
    if (galaxy::render::take_coop_chat_open()) {
        s_typing_here = true;
        if (available) (void)gCoopChat.open();
    }
    if (!available) {
        gCoopChat.close();
    } else if (gCoopChat.typing()) {
        const std::uint64_t now = galaxy::coop::chat_clock_ms();
        for (const char32_t key : galaxy::render::take_coop_text_input()) gCoopChat.type(key, now);
    }
    // Sent, dropped, or ended with the room: the keys play again.
    if (s_typing_here && !gCoopChat.typing()) {
        galaxy::render::set_coop_text_entry(false);
        s_typing_here = false;
    }
}
#endif

// The NebulaNet rooms are listed on: the game's, unless the settings name another (coop_nebulanet).
std::string coop_nebulanet_server() {
    const std::string& own = gCoopMenu.room().nebulanet_server;
    return own.empty() ? std::string(galaxy::coop::kNebulaNetServer) : own;
}

// A room code typed on the Join page ("K7QX2M", "k7qx-2m"), rather than an address (which has a dot or a colon).
std::optional<std::string> coop_join_code(const std::string& text) {
    if (text.find_first_of(".:[") != std::string::npos) return std::nullopt;
    return galaxy::coop::parse_room_code(text);
}

// Host: the room opens on the Host page's port as the save starts. False (and why on the menu) when it can't.
bool open_coop_room() {
    galaxy::coop::HostConfig config;
    config.room = gCoopMenu.room();
    config.port = config.room.port;
    config.game_version = kCoopGameVersion;
    config.name = coop_player_name();
    config.character = config.room.character;
    config.character_id = config.room.character_id;
    config.save = coop_room_save;
    config.disc = gCoopDisc;
    config.mods = coop_session_mods();
    config.bans = galaxy::get_runtime_settings().coop_bans;
    config.moderators = galaxy::get_runtime_settings().coop_moderators;
    // NebulaNet lists the room under a code (its chat line, and the Online tab, show it); Direct: by address only.
    if (config.room.nebulanet) config.nebulanet = coop_nebulanet_server();
    listen_coop_snapshots();
    if (!gCoopSession.host(std::move(config))) {
        std::fprintf(stderr, "[coop] the room could not open on port %u\n", static_cast<unsigned>(gCoopMenu.room().port));
        gCoopMenu.set_message("PORT " + std::to_string(gCoopMenu.room().port) +
                              " COULD NOT BE OPENED: ANOTHER PROGRAM HAS IT. PICK ANOTHER PORT");
        return false;
    }
    (void)gCoopSession.take_events();
    gCoopWorld.start();
    std::fprintf(stderr, "[coop] room open on UDP port %u, %s, mods: %s\n", static_cast<unsigned>(gCoopSession.port()),
                 galaxy::coop::disc_text(gCoopDisc).c_str(), galaxy::coop::mods_text(coop_session_mods()).c_str());
    note_coop_room();
    return true;
}

// The Rooms page's list (MenuCommand::ListRooms): NebulaNet's public rooms, or the private ones with the Join page's
// password, asked on a thread of its own and given to the menu as they come (poll_coop_rooms).
std::unique_ptr<galaxy::coop::NebulaNetLink> gCoopRoomList;

void start_coop_rooms() {
    static const std::shared_ptr<galaxy::net::Web> web = galaxy::net::platform_web();
    if (!gCoopRoomList) gCoopRoomList = std::make_unique<galaxy::coop::NebulaNetLink>(web);
    gCoopRoomList->list(coop_nebulanet_server(),
                        gCoopMenu.rooms_by_password() ? gCoopMenu.room().password : std::string());
}

void poll_coop_rooms() {
    if (!gCoopRoomList) return;
    for (galaxy::coop::NebulaNetEvent& event : gCoopRoomList->take_events()) {
        if (event.kind != galaxy::coop::NebulaNetEvent::Kind::Rooms) continue;
        std::vector<galaxy::coop::ListedRoom> rooms;
        for (galaxy::coop::NebulaNetRoom& room : event.rooms) {
            rooms.push_back({std::move(room.code), std::move(room.name), std::move(room.version), std::move(room.mode),
                             std::move(room.description), room.locked, room.players, room.max});
        }
        gCoopMenu.set_rooms(std::move(rooms), std::move(event.text), kCoopGameVersion);
        gCoopRoomList->stop();
    }
}

// Join: finds the Join page's room code on NebulaNet, or connects to its address. How it goes comes to
// poll_coop_join.
void start_coop_join() {
    keep_coop_settings();
    const galaxy::RuntimeCoopSettings& room = gCoopMenu.room();
    galaxy::coop::JoinConfig config;
    if (const std::optional<std::string> code = coop_join_code(room.join_address)) {
        config.code = *code;
        config.nebulanet = coop_nebulanet_server();
    } else if (!galaxy::parse_coop_endpoint(room.join_address, config.address, config.port)) {
        gCoopMenu.set_message("THAT ISN'T A ROOM CODE OR AN ADDRESS: HOST, HOST:PORT OR [IPV6]:PORT");
        return;
    }
    config.password = room.password;
    config.game_version = kCoopGameVersion;
    config.name = coop_player_name();
    config.character = room.character;
    config.character_id = room.character_id;
    config.disc = gCoopDisc;
    config.mods = coop_session_mods();
    config.mods_cache = coop_mods_cache();
    config.find_mod = find_own_mod;
    if (config.code.empty()) {
        std::fprintf(stderr, "[coop] joining %s port %u\n", config.address.c_str(), static_cast<unsigned>(config.port));
    } else {
        std::fprintf(stderr, "[coop] joining room %s on %s\n", config.code.c_str(), config.nebulanet.c_str());
    }
    listen_coop_snapshots();
    if (!gCoopSession.join(std::move(config))) {
        std::string why = "THE ADDRESS COULD NOT BE REACHED";
        for (const galaxy::coop::SessionEvent& event : gCoopSession.take_events()) {
            if (event.kind == galaxy::coop::SessionEvent::Kind::JoinFailed) why = event.text;
        }
        gCoopMenu.set_message("COULD NOT JOIN: " + why);
        return;
    }
    gCoopMenu.set_joining(true);
}

// A Join pressed in Discord (galaxy/discord_presence.h), on the game's thread: on the main menu it joins the room at
// once, as the Join page would with its code and password; in a game it waits for the main menu (the pause menu's
// Quit), as sm64coopdx would leave the game for it but without dropping a room or a save under the player.
void handle_discord_join(bool at_menu) {
    const std::optional<std::string> secret = galaxy::discord::take_join_secret();
    if (!secret) return;
    const std::optional<galaxy::discord::JoinRoom> join = galaxy::discord::parse_join_secret(*secret);
    const std::optional<std::string> code = join ? coop_join_code(join->code) : std::nullopt;
    if (!code) {
        std::fprintf(stderr, "[discord] Join: not a NebulaNet room\n");
        return;
    }
    std::string here = gCoopSession.room_code();
    if (here.empty() && gCoopSession.active()) here = coop_join_code(gCoopMenu.room().join_address).value_or("");
    if (here == *code && gCoopSession.state() != galaxy::coop::SessionState::Idle) {
        std::fprintf(stderr, "[discord] Join: already in room %s\n", code->c_str());
        return;
    }
    std::fprintf(stderr, "[discord] Join: room %s\n", code->c_str());
    if (!at_menu) {
        gCoopAutoStart = CoopAutoStart::Join;
        gCoopAutoAddress = *code;
        gCoopAutoPassword = join->password;
        gCoopChat.say("CoopDiscordJoin", *code);
        return;
    }
    if (gCoopMenu.joining()) {
        // Another join was under way: Discord's takes its place.
        gCoopMenu.set_joining(false);
        gCoopSession.leave();
        (void)gCoopSession.take_events();
    }
    galaxy::RuntimeCoopSettings room = gCoopMenu.room();
    room.join_address = *code;
    room.password = join->password;
    gCoopMenu.set_room(room);
    start_coop_join();
}

// While a join connects: true once the host let this player in and its save is ready to play.
bool poll_coop_join() {
    if (!gCoopMenu.joining()) return false;
    using Kind = galaxy::coop::SessionEvent::Kind;
    for (const galaxy::coop::SessionEvent& event : gCoopSession.take_events()) {
        if (event.kind == Kind::DownloadingMods) {
            std::fprintf(stderr, "[coop] downloading the room's mods: %s\n", event.text.c_str());
            continue;
        }
        if (event.kind == Kind::ModsReady) {
            gCoopMenu.set_joining(false);
            const std::optional<std::vector<galaxy::coop::SharedMod>> mods = gCoopSession.take_room_mods();
            if (mods) (void)restart_for_room_mods(*mods, gCoopMenu.room().join_address);
            return false;
        }
        if (event.kind == Kind::JoinFailed || event.kind == Kind::Closed) {
            gCoopMenu.set_joining(false);
            std::fprintf(stderr, "[coop] the join failed: %s\n", event.text.c_str());
            gCoopMenu.set_message("COULD NOT JOIN: " + (event.text.empty() ? std::string("CANCELLED") : event.text));
            return false;
        }
        if (event.kind != Kind::Joined) continue;
        const std::optional<galaxy::coop::RoomSave> save = gCoopSession.take_room_save();
        fs::path folder;
        if (!save || !write_joined_save(*save, folder)) {
            gCoopMenu.set_joining(false);
            gCoopSession.leave();
            (void)gCoopSession.take_events();
            gCoopMenu.set_message("COULD NOT JOIN: THE HOST'S SAVE COULD NOT BE WRITTEN");
            return false;
        }
        {
            const std::lock_guard<std::mutex> lock(gCoopSaveMutex);
            gCoopSaveFolder = folder;
        }
        gCoopSaveFile = save->file;
#if defined(NEBULA_HOST_RENDERER)
        // The save's achievements (its achievements.ini came with it), before the room's start coming.
        galaxy::achievements::set_file(folder / "achievements.ini");
#endif
        gCoopWorld.start();
        set_coop_character_wanted(gCoopMenu.room());
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::set_coop_in_game(true);
#endif
        std::fprintf(stderr, "[coop] joined: the host's save (file %u, %zu file(s)), %zu player(s)\n", save->file,
                     save->files.size(), gCoopSession.players().size());
        gCoopMenu.finish_join("JOINED! LOADING THE HOST'S SAVE...");
        // A moderator the host kept is one again (the rest of this join's events went with Joined).
        if (gCoopSession.is_moderator()) gCoopChat.say("CoopCmdModeratorGranted");
        note_coop_room();
        return true;
    }
    // The joining screen: what the join waits for, and how far (the mods' download by its bytes).
    using Step = galaxy::coop::JoinStep;
    switch (gCoopSession.join_step()) {
    case Step::FindingRoom: gCoopMenu.set_join_status("FINDING THE ROOM ON NEBULANET...", {}, 5u); break;
    case Step::Reaching: {
        // Its seconds, so a join the routers keep apart doesn't look frozen: it gives up with a reason at the limit.
        const galaxy::coop::JoinWait wait = gCoopSession.join_wait();
        const auto waited = std::chrono::duration_cast<std::chrono::seconds>(wait.waited).count();
        const auto limit = std::chrono::duration_cast<std::chrono::seconds>(wait.limit).count();
        std::string detail;
        if (limit > 0 && waited >= 3) {
            detail = (wait.relaying ? "TRYING FOR " : "NO ANSWER YET: TRYING FOR ") + std::to_string(waited) + " OF " +
                     std::to_string(limit) + " SECONDS";
        }
        // A join that heard nothing back tries a relay too (NebulaNet's TURN servers).
        gCoopMenu.set_join_status(wait.relaying ? "CONNECTING THROUGH A RELAY..." : "CONNECTING TO THE HOST...",
                                  std::move(detail), wait.relaying ? 20u : 10u);
        break;
    }
    case Step::Asking: gCoopMenu.set_join_status("WAITING FOR THE HOST TO LET YOU IN...", {}, 30u); break;
    case Step::DownloadingMods: {
        const galaxy::coop::DownloadProgress progress = gCoopSession.download_progress();
        const unsigned percent =
            progress.total == 0u ? 100u : static_cast<unsigned>(progress.done * 100u / progress.total);
        gCoopMenu.set_join_status("DOWNLOADING THE ROOM'S MODS: " + std::to_string(percent) + "%", progress.mod,
                                  30u + percent * 60u / 100u);
        break;
    }
    case Step::None: break;
    }
    return false;
}

// The menu's settings (the room's, the player's, the last address) as the runtime settings, saved as they change.
void keep_coop_settings() {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::RuntimeSettings settings = galaxy::get_runtime_settings();
    // The player's name, character and colours (CUSTOM's kept ones too) are Options' (its PLAYER page): the menu takes
    // them, never gives them back.
    if (gCoopMenu.room().player_name != settings.coop.player_name ||
        gCoopMenu.room().character != settings.coop.character || gCoopMenu.room().character_id != settings.coop.character_id ||
        gCoopMenu.room().palette != settings.coop.palette || gCoopMenu.room().custom_palette != settings.coop.custom_palette) {
        galaxy::RuntimeCoopSettings room = gCoopMenu.room();
        room.player_name = settings.coop.player_name;
        room.character = settings.coop.character;
        room.character_id = settings.coop.character_id;
        room.palette = settings.coop.palette;
        room.custom_palette = settings.coop.custom_palette;
        gCoopMenu.set_room(room);
    }
    if (settings.coop == gCoopMenu.room()) return;
    settings.coop = gCoopMenu.room();
    galaxy::set_runtime_settings(settings);
    (void)galaxy::save_runtime_settings_to_file();
#endif
}

// /permban and /moderator: who they named, kept with the player's settings for every room it hosts.
void keep_coop_list(bool ban, const std::string& identity) {
    galaxy::RuntimeSettings settings = galaxy::get_runtime_settings();
    std::vector<std::string>& list = ban ? settings.coop_bans : settings.coop_moderators;
    if (identity.empty() || std::find(list.begin(), list.end(), identity) != list.end()) return;
    list.push_back(identity);
    galaxy::set_runtime_settings(settings);
    (void)galaxy::save_runtime_settings_to_file();
}

// The mods folder's mods for the Host page: mods.ini's on and off (the room last hosted), the ones on in their order.
std::vector<galaxy::coop::RoomMod> coop_room_mods() {
    std::vector<galaxy::coop::RoomMod> room;
    if (galaxy::mods::mods_folder().empty()) return room;
    for (const galaxy::mods::ModInfo& mod : galaxy::mods::scan_mods()) room.push_back({mod.id, mod.enabled});
    return room;
}

// The Host page's mods to mods.ini (in its order, the ones on first); texture packs and movies apply at once. True when
// the disc's mods now differ from the ones this session started with: a restart applies them.
bool keep_coop_mods() {
    if (galaxy::mods::mods_folder().empty()) return false;
    std::vector<galaxy::mods::ModInfo> scanned = galaxy::mods::scan_mods();
    if (gCoopMenu.mods_changed()) {
        std::vector<galaxy::mods::ModInfo> ordered;
        for (const galaxy::coop::RoomMod& room : gCoopMenu.room_mods()) {
            const auto it = std::find_if(scanned.begin(), scanned.end(),
                                         [&room](const galaxy::mods::ModInfo& mod) { return mod.id == room.id; });
            if (it == scanned.end()) continue;
            it->enabled = room.on;
            ordered.push_back(std::move(*it));
            scanned.erase(it);
        }
        // Mods new since the page read the folder stay as mods.ini has them, after the room's.
        for (galaxy::mods::ModInfo& mod : scanned) ordered.push_back(std::move(mod));
        std::stable_partition(ordered.begin(), ordered.end(), [](const galaxy::mods::ModInfo& mod) { return mod.enabled; });
        for (std::size_t i = 0; i < ordered.size(); ++i) ordered[i].order = static_cast<unsigned>(i + 1u);
        if (!galaxy::mods::save_mod_settings(ordered)) {
            std::fprintf(stderr, "[coop] mods.ini could not be written\n");
        }
        scanned = std::move(ordered);
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::set_texture_pack_mod_folders(galaxy::mods::texture_folders(scanned));
        galaxy::render::set_movie_mod_folders(galaxy::mods::movie_folders(scanned));
#endif
    }
    return galaxy::mods::mods_differ_from_session(scanned);
}

// Host's save, as Play leaves the menu: made when new, then the game's save folder (save_folder_override), with its
// achievements; the room's settings and mods are kept. When the room's mods change the disc, Nebula starts again to
// apply them and plays the save once back at the menu. False when it doesn't play now.
bool start_coop_save() {
    keep_coop_settings();
    if (!gCoopSaves) return false;
    std::string id = gCoopMenu.chosen_save();
    if (id.empty()) id = gCoopSaves->create();
    const std::optional<galaxy::coop::SaveInfo> save = gCoopSaves->find(id);
    if (!save) {
        std::fprintf(stderr, "[coop] the save could not be made in %s\n", gCoopSaves->root().string().c_str());
        return false;
    }
    if (keep_coop_mods()) {
        bool restarting = false;
#if defined(NEBULA_HOST_RENDERER)
        restarting = gCoopSaves->set_next_start(id) && galaxy::render::request_restart();
#endif
        if (restarting) {
            std::fprintf(stderr, "[coop] restarting for the room's mods, then hosting %s\n", id.c_str());
            gCoopMenu.set_message("NEBULA STARTS AGAIN WITH THE ROOM'S MODS, THEN HOSTS");
            return false;
        }
        (void)gCoopSaves->take_next_start();
        gCoopMenu.set_message("THE ROOM'S MODS APPLY WHEN NEBULA STARTS AGAIN: CLOSE IT AND START IT AGAIN TO HOST");
        return false;
    }
    if (!open_coop_room()) return false;
    (void)gCoopSaves->mark_played(id, static_cast<std::int64_t>(std::time(nullptr)));
    const fs::path folder = gCoopSaves->folder(id);
    {
        const std::lock_guard<std::mutex> lock(gCoopSaveMutex);
        gCoopSaveFolder = folder;
    }
    gCoopSaveFile = save->file;
    set_coop_character_wanted(gCoopMenu.room());
#if defined(NEBULA_HOST_RENDERER)
    galaxy::achievements::set_file(folder / "achievements.ini");
    galaxy::render::set_coop_in_game(true);
#endif
    std::fprintf(stderr, "[coop] hosting save %s (%s, file %u) as %s\n", save->id.c_str(), save->name.c_str(),
                 save->file, galaxy::coop_character_key(gCoopMenu.room().character));
    return true;
}

// The old saves (the NAND's, before co-op): once, a co-op save from each user file they hold, with the achievements
// of then. Not after a co-op save played: the game's files are its then.
void convert_old_saves(unsigned files) {
    {
        const std::lock_guard<std::mutex> lock(gCoopSaveMutex);
        if (!gCoopSaveFolder.empty()) return;
    }
    if (!gCoopSaves || gCoopSaves->converted() || gSaveDataPath.empty()) return;
    const fs::path old_saves = gSaveRoot / gSaveDataPath.substr(1);
    fs::path achievements;
#if defined(NEBULA_HOST_RENDERER)
    const fs::path settings_folder = galaxy::runtime_settings_folder();
    if (!settings_folder.empty()) achievements = settings_folder / "achievements.ini";
#endif
    if (gCoopSaves->convert(old_saves, files & 0x3fu, achievements)) {
        std::fprintf(stderr, "[coop] the old saves' files (0x%02x) are co-op saves now\n", files & 0x3fu);
    }
}

bool host_settings_open() {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::native_settings_view().open;
#else
    return false;
#endif
}

void fill_coop_menu_view(NebulaSettingsView* view, uint32_t input) {
#if defined(NEBULA_HOST_RENDERER)
    // What the keyboard typed into a text row; while one is edited the window's keys type instead of playing.
    for (const char32_t key : galaxy::render::take_coop_text_input()) gCoopMenu.type(key);
    galaxy::render::set_coop_text_entry(gCoopMenu.editing());
#endif
    const galaxy::coop::MenuView menu = gCoopMenu.view();
    SettingsContent next;
    next.kind = NEBULA_SETTINGS_KIND_MAIN_MENU;
    next.open = menu.open ? 1u : 0u;
    next.page = menu.page;
    next.pages = menu.pages;
    next.input = input;
    next.rows.reserve(menu.rows.size());
    for (const auto& row : menu.rows) {
        SettingsRowContent& out = next.rows.emplace_back();
        out.label = row.label;
        out.value = row.value;
        out.flags = (row.buttons ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_BUTTONS) : 0u) |
                    (row.changes ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_CHANGES) : 0u) |
                    (row.name ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_NAME) : 0u) |
                    (row.text ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_TEXT) : 0u) |
                    (row.label_name ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_LABEL_NAME) : 0u) |
                    (row.bar_steps != 0u ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_SLIDER) : 0u);
        // A loading bar: a slider that doesn't change (no NEBULA_SETTINGS_ROW_CHANGES).
        out.slider_steps = static_cast<uint16_t>(row.bar_steps);
        out.slider_at = static_cast<uint16_t>(row.bar_at);
    }
    next.selected = menu.selected;
    next.selected_button = menu.selected_button;
    next.back = menu.back ? 1u : 0u;
    next.editing = menu.editing ? 1u : 0u;
    if (menu.selected < menu.rows.size()) next.help = menu.rows[menu.selected].help;
    publish_settings_view(std::move(next), view);
}

// The strings, the page names and the rows are held here, whole, until the next call. The revision changes whenever the
// content differs from the previous call's. The headless host has no settings menu: always closed, nothing pointed at.
void svc_settings_view(void*, NebulaSettingsView* view) {
    std::memset(view, 0, sizeof(*view));
#if defined(NEBULA_HOST_RENDERER)
    const galaxy::render::NativeSettingsView source = galaxy::render::native_settings_view();
    if (!source.open && gCoopMenu.is_open()) {
        // The hints show what the player last used (or the chosen button icons), as the settings' do: a controller's
        // buttons, not the keyboard's keys, once a controller moves the menu.
        fill_coop_menu_view(view, source.input);
        return;
    }
    SettingsContent next;
    next.open = source.open ? 1u : 0u;
    g_settings_open_views = source.open ? g_settings_open_views + 1u : 0u;
    next.page = source.page;
    next.pages = source.pages;
    next.controls_page = source.controls_page;
    next.input = source.input;
    const std::size_t total = source.rows.size();
    const std::size_t selected = std::min<std::size_t>(source.selected, total == 0u ? 0u : total - 1u);
    next.rows.reserve(total);
    for (std::size_t r = 0; r < total; ++r) {
        const galaxy::render::NativeSettingsViewRow& row = source.rows[r];
        SettingsRowContent& out = next.rows.emplace_back();
        out.label = row.label;
        out.value = row.value;
        out.flags = (row.heading ? NEBULA_SETTINGS_ROW_HEADING : 0u) |
                    (row.changes ? NEBULA_SETTINGS_ROW_CHANGES : 0u) | (row.name ? NEBULA_SETTINGS_ROW_NAME : 0u) |
                    (row.active ? NEBULA_SETTINGS_ROW_ACTIVE : 0u) |
                    (row.label_name ? NEBULA_SETTINGS_ROW_LABEL_NAME : 0u) |
                    (row.buttons ? NEBULA_SETTINGS_ROW_BUTTONS : 0u) |
                    (row.mod_list == 1u ? NEBULA_SETTINGS_ROW_MOD_OFF : 0u) |
                    (row.mod_list == 2u ? NEBULA_SETTINGS_ROW_MOD_ON : 0u) | (row.held ? NEBULA_SETTINGS_ROW_HELD : 0u) |
                    (row.slider_steps != 0u ? NEBULA_SETTINGS_ROW_SLIDER : 0u) |
                    (row.inputs ? NEBULA_SETTINGS_ROW_BINDING : 0u) |
                    (row.shared ? NEBULA_SETTINGS_ROW_SHARED : 0u) |
                    (row.text ? NEBULA_SETTINGS_ROW_TEXT : 0u);
        out.slider_steps = static_cast<uint16_t>(row.slider_steps);
        out.slider_at = static_cast<uint16_t>(row.slider_at);
        if (row.mod_list != 0u && row.mod_list <= 2u && !row.heading) ++next.mod_list_count[row.mod_list - 1u];
    }
    next.selected = static_cast<uint32_t>(selected);
    next.selected_button = source.selected_button;
    next.editing = source.editing ? 1u : 0u;
    next.help = source.help;
    next.help_as_is = source.help_as_is ? 1u : 0u;
    next.status = static_cast<uint32_t>(source.status);
    next.status_number = source.status_number;
    next.status_text = source.status_text;
    next.long_text = source.long_text;
    next.picture = source.picture;
    next.graph = source.picture != nullptr ? source.graph : galaxy::render::NativeSettingsGraph{};
    next.scene_live = galaxy::calibration_scene::live() ? 1u : 0u;

    publish_settings_view(std::move(next), view);
#else
    if (gCoopMenu.is_open()) fill_coop_menu_view(view, NEBULA_SETTINGS_INPUT_KEYBOARD);
#endif
}

// The benchmark's run as the game's run panel shows it (benchmark.h live_state).
int32_t svc_benchmark_state(void*, NebulaBenchmarkState* state) {
#if defined(NEBULA_HOST_RENDERER)
    static_assert(static_cast<uint32_t>(galaxy::bench::Part::Yours) == NEBULA_BENCHMARK_PART_YOURS &&
                      static_cast<uint32_t>(galaxy::bench::Part::Standard) == NEBULA_BENCHMARK_PART_STANDARD &&
                      static_cast<uint32_t>(galaxy::bench::Part::Probe) == NEBULA_BENCHMARK_PART_PROBE &&
                      static_cast<uint32_t>(galaxy::bench::Part::Again) == NEBULA_BENCHMARK_PART_AGAIN,
                  "the benchmark's parts are numbered alike");
    galaxy::bench::LiveState live;
    if (state == nullptr || !galaxy::bench::live_state(live)) return 0;
    std::memset(state, 0, sizeof(*state));
    state->step = live.step;
    state->steps = live.steps;
    state->part = static_cast<uint32_t>(live.part);
    state->showing = live.showing ? 1u : 0u;
    state->fps_tenths = live.fps_tenths;
    state->progress = live.progress;
    state->stopping = live.stopping ? 1u : 0u;
    state->input = galaxy::render::native_settings_hint_input();
    state->cover = std::min(live.cover, 255u);
    std::snprintf(state->stage, sizeof(state->stage), "%s", live.stage.c_str());
    return 1;
#else
    (void)state;
    return 0;
#endif
}

// The pointer's input to the settings menu: NEBULA_SETTINGS_* are galaxy::render::NativeSettingsInput's values.
void svc_settings_input(void*, uint32_t action, int32_t arg) {
    if (gCoopMenu.is_open() && !host_settings_open()) {
        switch (action) {
        case NEBULA_SETTINGS_SELECT_PAGE: gCoopMenu.select_page(static_cast<unsigned>(std::max(arg, 0))); break;
        case NEBULA_SETTINGS_SELECT_ROW: gCoopMenu.select_row(static_cast<unsigned>(std::max(arg, 0))); break;
        case NEBULA_SETTINGS_MOVE: gCoopMenu.move(arg); break;
        case NEBULA_SETTINGS_CHANGE: gCoopMenu.change(arg); break;
        case NEBULA_SETTINGS_PRESS: gCoopMenu.press(static_cast<unsigned>(std::max(arg, 0))); break;
        case NEBULA_SETTINGS_CLOSE: gCoopMenu.back(); break;
        case NEBULA_SETTINGS_REVERT: gCoopMenu.reset(); break;
        default: break;  // nothing to apply or revert: the main menu stays until a choice leaves it
        }
        return;
    }
#if defined(NEBULA_HOST_RENDERER)
    if (action == NEBULA_SETTINGS_CLOSE && gSceneBegins.load(std::memory_order_relaxed) == 0u && g_settings_open_views <= 1u) {
        // Before the game's first 3D scene (the boot) the game closes a menu asked for there in the frame it first reads
        // it open, without drawing it: that close answers nothing, and a menu with a question is hidden until the first
        // 3D scene (svc_submit_frame). A close in a later frame is a press on the menu the game shows (its launcher, a
        // question it lets stand in the boot) and answers as Escape.
        galaxy::render::native_settings_input(galaxy::render::NativeSettingsInput::CloseUnseen, arg);
    } else if (action >= NEBULA_SETTINGS_OPEN && action <= NEBULA_SETTINGS_SET_VALUE) {
        galaxy::render::native_settings_input(static_cast<galaxy::render::NativeSettingsInput>(action), arg);
    }
#else
    (void)action, (void)arg;
#endif
}

// The chat as the game draws it: the lines showing now and the line typed, only in a room's game.
void svc_coop_chat(void*, NebulaCoopChatView* view) {
    std::memset(view, 0, sizeof(*view));
#if defined(NEBULA_HOST_RENDERER)
    if (!gCoopSession.active() || !galaxy::render::coop_in_game()) return;
#endif
    const galaxy::coop::ChatView chat = gCoopChat.view(galaxy::coop::chat_clock_ms());
    const auto copy = [](char* out, std::size_t size, const std::string& text) {
        std::snprintf(out, size, "%s", text.c_str());
    };
    for (const galaxy::coop::ChatView::Line& shown : chat.lines) {
        if (view->line_count >= NEBULA_COOP_CHAT_LINES) break;
        NebulaCoopChatLine& line = view->lines[view->line_count++];
        line.id = shown.line.id;
        switch (shown.line.kind) {
        case galaxy::coop::ChatLine::Kind::Joined: line.kind = NEBULA_COOP_CHAT_JOINED; break;
        case galaxy::coop::ChatLine::Kind::Left: line.kind = NEBULA_COOP_CHAT_LEFT; break;
        case galaxy::coop::ChatLine::Kind::Notice: line.kind = NEBULA_COOP_CHAT_NOTICE; break;
        default: line.kind = NEBULA_COOP_CHAT_MESSAGE; break;
        }
        line.character = shown.line.character == galaxy::CoopCharacter::Luigi ? 1u : 0u;
        line.alpha = static_cast<uint32_t>(std::clamp(shown.alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        copy(line.name, sizeof(line.name), shown.line.name);
        copy(line.text, sizeof(line.text), shown.line.text);
    }
    view->typing = chat.typing ? 1u : 0u;
    view->input_serial = chat.input_serial;
    view->cursor = static_cast<uint32_t>(std::min<std::size_t>(chat.cursor, sizeof(view->input) - 1u));
    copy(view->input, sizeof(view->input), chat.input);
}

int32_t svc_coop_menu(void*, uint32_t what) {
    switch (what) {
    case NEBULA_COOP_MENU_OPEN:
#if defined(NEBULA_HOST_RENDERER)
        if (!gCoopMenu.is_open()) gCoopMenu.set_room(galaxy::get_runtime_settings().coop);
        galaxy::render::set_coop_in_game(false);
#endif
        if (!gCoopMenu.is_open()) {
            // Back at the main menu: the game ended, and with it the room.
            if (gCoopSession.active()) gCoopSession.leave();
            (void)gCoopSession.take_events();
            gCoopChat.clear();
            gCoopPopups.clear();
            {
                const std::lock_guard<std::mutex> lock(gCoopLock);
                gCoopLocalValid = false;
            }
            note_coop_place();
            note_coop_room();
            gCoopMenu.set_joining(false);
            gCoopMenu.open();
            if (std::string notice = take_coop_notice(); !notice.empty()) gCoopMenu.set_message(notice);
            // Back after a restart for the room's mods: its save plays at once.
            std::string next = gCoopSaves ? gCoopSaves->take_next_start() : std::string();
            if (!next.empty()) {
                gCoopMenu.play(std::move(next));
            } else if (const CoopAutoStart start = std::exchange(gCoopAutoStart, CoopAutoStart::None);
                       start == CoopAutoStart::Host) {
                // --host: the last played save, on the port given.
                galaxy::RuntimeCoopSettings room = gCoopMenu.room();
                room.port = gCoopAutoPort;
                gCoopMenu.set_room(room);
                const std::vector<galaxy::coop::SaveInfo> saves =
                    gCoopSaves ? gCoopSaves->list() : std::vector<galaxy::coop::SaveInfo>{};
                gCoopMenu.play(saves.empty() ? std::string() : saves.front().id);
            } else if (start == CoopAutoStart::Join) {
                galaxy::RuntimeCoopSettings room = gCoopMenu.room();
                room.join_address = gCoopAutoAddress;
                if (std::optional<std::string> password = std::exchange(gCoopAutoPassword, std::nullopt)) {
                    room.password = std::move(*password);
                }
                gCoopMenu.set_room(room);
                start_coop_join();
            }
        }
        return 0;
    case NEBULA_COOP_MENU_CLOSE:
        gCoopMenu.close();
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::set_coop_text_entry(false);
#endif
        return 0;
    case NEBULA_COOP_MENU_CHARACTER:
#if defined(NEBULA_HOST_RENDERER)
        // Live: Options' PLAYER page may switch it mid-game (sm64coopdx's live character change). Not in the main
        // menu, where Host and Join take the menu's own.
        if (!gCoopMenu.is_open()) {
            set_coop_character_wanted(galaxy::get_runtime_settings().coop);
        }
#endif
        return gCoopLuigi.load(std::memory_order_relaxed) ? 1 : 0;
    case NEBULA_COOP_MENU_JOINED: return gCoopSession.active() && !gCoopSession.is_host() ? 1 : 0;
    case NEBULA_COOP_MENU_SAVE_LIVES:
        // A joiner plays the host's save, whose lives are the host's: theirs start afresh.
        if (gCoopSession.active() && !gCoopSession.is_host()) return 0;
        return (gCoopSession.is_host() ? gCoopSession.room() : gCoopMenu.room()).save_lives ? 1 : 0;
    case NEBULA_COOP_MENU_LEAVE:
        // Each game scene frame: the room's news, the chat's line, and the room ending under the player.
        pump_coop_session();
        handle_discord_join(false);
#if defined(NEBULA_HOST_RENDERER)
        pump_coop_chat();
        if (galaxy::render::take_leave_game()) {
            // The room ended (the host quit, or this player was let go): out of it, then the main menu.
            if (gCoopSession.active()) {
                std::fprintf(stderr, "[coop] leaving the room\n");
                gCoopSession.leave();
                (void)gCoopSession.take_events();
                note_coop_room();
            }
            gCoopChat.clear();
            return 1;
        }
#endif
        return 0;
    case NEBULA_COOP_MENU_TITLE: {
        // The title once, as Nebula starts; not in front of a room the menu starts by itself as it opens.
        static bool shown = false;
        if (std::exchange(shown, true)) return 0;
        // Nor when Discord started Nebula for a Join: the main menu joins its room at once.
        const bool starting = gCoopAutoStart != CoopAutoStart::None || (gCoopSaves && gCoopSaves->has_next_start()) ||
                              galaxy::discord::join_pending();
        return starting ? 0 : 1;
    }
    case NEBULA_COOP_MENU_POLL:
        // What the player changed is kept as they change it, a text once kept.
        if (!gCoopMenu.editing()) keep_coop_settings();
        if (!gCoopMenu.editing()) handle_discord_join(true);
        if (poll_coop_join()) return NEBULA_COOP_MENU_PLAY;
        poll_coop_rooms();
        switch (gCoopMenu.take_command()) {
        case galaxy::coop::MenuCommand::Play:
            if (gCoopMenu.joining()) return NEBULA_COOP_MENU_NONE;
            return start_coop_save() ? NEBULA_COOP_MENU_PLAY : NEBULA_COOP_MENU_NONE;
        case galaxy::coop::MenuCommand::Join:
            if (!gCoopMenu.joining()) start_coop_join();
            return NEBULA_COOP_MENU_NONE;
        case galaxy::coop::MenuCommand::CancelJoin:
            gCoopMenu.set_joining(false);
            gCoopSession.leave();
            (void)gCoopSession.take_events();
            std::fprintf(stderr, "[coop] the join was cancelled\n");
            return NEBULA_COOP_MENU_NONE;
        case galaxy::coop::MenuCommand::ListRooms:
            start_coop_rooms();
            return NEBULA_COOP_MENU_NONE;
        case galaxy::coop::MenuCommand::Quit: return NEBULA_COOP_MENU_QUIT;
        case galaxy::coop::MenuCommand::Settings:
#if defined(NEBULA_HOST_RENDERER)
            galaxy::render::native_settings_input(galaxy::render::NativeSettingsInput::Open, 0);
#endif
            return NEBULA_COOP_MENU_NONE;
        case galaxy::coop::MenuCommand::None: break;
        }
        return NEBULA_COOP_MENU_NONE;
    case NEBULA_COOP_MENU_SAVE_FILE: return static_cast<int32_t>(gCoopSaveFile);
    case NEBULA_COOP_MENU_KEYS:
#if defined(NEBULA_HOST_RENDERER)
        return static_cast<int32_t>(galaxy::render::coop_keys_held());
#else
        return 0;
#endif
    default:
        if ((what & 0xffu) == NEBULA_COOP_MENU_OLD_FILES) convert_old_saves(what >> 8u);
        return 0;
    }
}

// The mods' chat commands (the coop service's add_command, sm64coopdx's hook_chat_command) this player typed, for the
// game's next coop_command_take: each one's name and its word's place.
std::mutex gModCommandLock;
std::deque<std::pair<std::string, int32_t>> gModCommandsWaiting;

// A mod's chat command, on the chat's: shown in a room, each one typed sent to the game (coop_command_take). 1: added;
// 0: taken or not a name.
int svc_coop_command_add(void*, const char* name, const char* description, const char* options) {
    if (name == nullptr || description == nullptr) return 0;
    const auto in_room = [] {
        const galaxy::coop::SessionState state = gCoopSession.state();
        return state == galaxy::coop::SessionState::Hosting || state == galaxy::coop::SessionState::Joined;
    };
    std::vector<std::string> words;
    for (std::string_view rest = options != nullptr ? options : ""; !rest.empty();) {
        const std::size_t space = rest.find(' ');
        if (space != 0u) words.emplace_back(rest.substr(0, space));
        if (space == std::string_view::npos) break;
        rest.remove_prefix(space + 1u);
    }
    return galaxy::coop::add_mod_command(
               gCoopChat.commands(), name, description, std::move(words),
               [](const std::string& command, int word) {
                   const std::lock_guard<std::mutex> lock(gModCommandLock);
                   // Never more than a few: one typed line is one command, and the game takes them each frame.
                   if (gModCommandsWaiting.size() < 16u) gModCommandsWaiting.emplace_back(command, static_cast<int32_t>(word));
               },
               in_room)
               ? 1
               : 0;
}

int svc_coop_command_take(void*, char* command, uint32_t capacity, int32_t* word) {
    if (command == nullptr || capacity == 0u || word == nullptr) return 0;
    const std::lock_guard<std::mutex> lock(gModCommandLock);
    if (gModCommandsWaiting.empty()) return 0;
    std::snprintf(command, capacity, "%s", gModCommandsWaiting.front().first.c_str());
    *word = gModCommandsWaiting.front().second;
    gModCommandsWaiting.pop_front();
    return 1;
}

void svc_coop_chat_say(void*, const char* utf8) {
    if (utf8 == nullptr || utf8[0] == '\0') return;
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state != galaxy::coop::SessionState::Hosting && state != galaxy::coop::SessionState::Joined) return;
    gCoopChat.say(std::string(utf8));
}

// A teleport the cheat commands asked for (galaxy/coop_teleport.h), for the game's next coop_world_take.
std::mutex gCoopTeleportLock;
std::optional<galaxy::coop::Position> gCoopTeleport;

// /tp, /teleport and /pos, over the game's last report of this player and the others' snapshots.
void add_coop_cheat_commands() {
    galaxy::coop::CheatHooks hooks;
    hooks.here = []() -> std::optional<galaxy::coop::Position> {
        const std::lock_guard<std::mutex> lock(gCoopLock);
        if (!gCoopLocalValid || gCoopLocal.stage[0] == '\0') return std::nullopt;
        return galaxy::coop::Position{gCoopLocal.position[0], gCoopLocal.position[1], gCoopLocal.position[2]};
    };
    hooks.where = [](galaxy::net::PeerId id) -> std::optional<galaxy::coop::Position> {
        std::string stage;
        int scenario = 0;
        {
            const std::lock_guard<std::mutex> lock(gCoopLock);
            if (!gCoopLocalValid || gCoopLocal.stage[0] == '\0') return std::nullopt;
            stage = gCoopLocal.stage;
            scenario = gCoopLocal.scenario;
        }
        for (const galaxy::coop::RemotePlayer& player : gCoopRemote.sample(stage, scenario)) {
            if (player.id == id) return player.snapshot.position;
        }
        return std::nullopt;
    };
    hooks.move = [](const galaxy::coop::Position& position) {
        const std::lock_guard<std::mutex> lock(gCoopTeleportLock);
        gCoopTeleport = position;
    };
    galaxy::coop::add_cheat_commands(gCoopChat, gCoopSession, std::move(hooks));
}

// The player list (Tab or the controller's Share held): the room's players by number, each one's place and round trip.
uint32_t svc_coop_list(void*, NebulaCoopListPlayer* players, uint32_t capacity, uint32_t* room_size) {
    if (room_size != nullptr) *room_size = 0u;
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state != galaxy::coop::SessionState::Hosting && state != galaxy::coop::SessionState::Joined) return 0u;
    const std::vector<galaxy::coop::SessionPlayer> list = gCoopSession.players();
    const galaxy::RuntimeCoopSettings room = gCoopSession.room();
    if (room_size != nullptr) *room_size = room.max_players;
    const std::size_t count = std::min<std::size_t>(list.size(), players != nullptr ? capacity : 0u);
    for (std::size_t i = 0; i < count; ++i) {
        const galaxy::coop::SessionPlayer& from = list[i];
        NebulaCoopListPlayer& to = players[i];
        to = {};
        std::snprintf(to.name, sizeof(to.name), "%s", from.name.c_str());
        std::snprintf(to.stage, sizeof(to.stage), "%s", from.stage.c_str());
        to.scenario = from.scenario;
        to.number = from.index + 1u;
        to.ping_ms = from.ping_ms;
        to.stars = from.stars;
        to.flags = (from.character == galaxy::CoopCharacter::Luigi ? NEBULA_COOP_LIST_LUIGI : 0u) |
                   (from.host ? NEBULA_COOP_LIST_HOST : 0u) | (from.local ? NEBULA_COOP_LIST_YOU : 0u);
        std::snprintf(to.character, sizeof(to.character), "%s", from.character_id.c_str());
        to.has_palette = from.has_palette ? 1u : 0u;
        std::memcpy(to.palette, from.palette.data(), sizeof(to.palette));
    }
    return static_cast<uint32_t>(list.size());
}

// What the game is told and hasn't taken yet (coop_world_take), in order.
std::deque<galaxy::coop::WorldEvent> gCoopWorldWaiting;
// The others' objects' states as they came, the game's next to take (coop_objects_take).
std::deque<galaxy::coop::ObjectState> gCoopStatesWaiting;

// The shared world: what the game's play did (galaxy::coop::World settles it, or sends it to the host to).
void svc_coop_world_report(void*, const NebulaCoopWorldEvent* event) {
    if (event == nullptr || !gCoopWorld.active()) return;
    NebulaCoopWorldEvent copy = *event;
    copy.name[sizeof(copy.name) - 1] = '\0';
    using galaxy::coop::SaveKind;
    switch (copy.kind) {
    case NEBULA_COOP_WORLD_ENTER:
        std::fprintf(stderr, "[coop] world: %s, scenario %d placed\n", copy.name, static_cast<int>(copy.scenario));
        // Nothing waiting from the last scene is for this one (the save comes again whole).
        gCoopWorldWaiting.clear();
        gCoopStatesWaiting.clear();
        gCoopWorld.enter({copy.name, copy.scenario});
        return;
    case NEBULA_COOP_WORLD_GONE: gCoopWorld.report_gone({copy.object, copy.value}); return;
    case NEBULA_COOP_WORLD_STAR: gCoopWorld.report_save({SaveKind::Star, copy.name, copy.scenario, 0u}); return;
    case NEBULA_COOP_WORLD_FLAG: gCoopWorld.report_save({SaveKind::Flag, copy.name, 0, 0u}); return;
    case NEBULA_COOP_WORLD_VALUE: gCoopWorld.report_save({SaveKind::Value, copy.name, 0, copy.value}); return;
    case NEBULA_COOP_WORLD_COINS:
        if (copy.value > 0xffffu) return;
        gCoopWorld.report_save({SaveKind::Coins, copy.name, copy.scenario, copy.value});
        return;
    case NEBULA_COOP_WORLD_RACE_TIME: gCoopWorld.report_save({SaveKind::RaceTime, copy.name, 0, copy.value}); return;
    case NEBULA_COOP_WORLD_STAR_BITS:
        // The bank (the host's is everyone's), and the change to it (a joiner's goes to the host's game).
        if (copy.value <= 0xffffu) gCoopWorld.report_save({SaveKind::StarBits, "", 0, copy.value});
        if (copy.object != 0u) gCoopWorld.report_save({SaveKind::StarBitsChange, "", 0, copy.object});
        return;
    case NEBULA_COOP_WORLD_STAR_GOT:
        gCoopWorld.report_star({copy.scenario, (copy.value & NEBULA_COOP_STAR_GRAND) != 0u});
        return;
    case NEBULA_COOP_WORLD_HIT:
        if (copy.value > 0xffffu || copy.scenario < 0 || copy.scenario > 255) return;
        gCoopWorld.report_hit({copy.object, static_cast<std::uint16_t>(copy.value),
                               static_cast<std::uint8_t>(copy.scenario), 0u});
        return;
    case NEBULA_COOP_WORLD_SWITCH: gCoopWorld.report_switch({copy.object, copy.value != 0u}); return;
    case NEBULA_COOP_WORLD_CLOCK: gCoopWorld.report_clock(copy.value); return;
    case NEBULA_COOP_WORLD_MISS:
        std::fprintf(stderr, "[coop] world: a miss, the instance goes on without this player until its scene starts over\n");
        gCoopWorld.report_miss();
        return;
    default: return;
    }
}

// What the game is told, in order: what it can't take this call waits for the next.
uint32_t svc_coop_world_take(void*, NebulaCoopWorldEvent* events, uint32_t capacity) {
    std::deque<galaxy::coop::WorldEvent>& waiting = gCoopWorldWaiting;
    std::optional<galaxy::coop::Position> teleport;
    {
        const std::lock_guard<std::mutex> lock(gCoopTeleportLock);
        teleport = std::exchange(gCoopTeleport, std::nullopt);
    }
    if (!gCoopWorld.active()) {
        waiting.clear();
        return 0u;
    }
    for (galaxy::coop::WorldEvent& event : gCoopWorld.take()) waiting.push_back(std::move(event));
    uint32_t count = 0u;
    // A cheat command's teleport first: the position's floats' bits in object, value and scenario.
    if (teleport && events != nullptr && capacity > 0u) {
        NebulaCoopWorldEvent& to = events[count++];
        to = {};
        to.kind = NEBULA_COOP_WORLD_TELEPORT;
        to.object = std::bit_cast<uint32_t>((*teleport)[0]);
        to.value = std::bit_cast<uint32_t>((*teleport)[1]);
        to.scenario = std::bit_cast<int32_t>((*teleport)[2]);
    }
    while (events != nullptr && count < capacity && !waiting.empty()) {
        const galaxy::coop::WorldEvent& from = waiting.front();
        NebulaCoopWorldEvent& to = events[count++];
        to = {};
        using Kind = galaxy::coop::WorldEvent::Kind;
        if (from.kind == Kind::Ready) {
            to.kind = NEBULA_COOP_WORLD_READY;
        } else if (from.kind == Kind::Gone) {
            to.kind = NEBULA_COOP_WORLD_GONE;
            to.object = from.gone.object;
            to.value = from.gone.how;
        } else if (from.kind == Kind::StarGot) {
            to.kind = NEBULA_COOP_WORLD_STAR_GOT;
            to.scenario = from.star.star;
            to.value = from.star.grand ? NEBULA_COOP_STAR_GRAND : 0u;
        } else if (from.kind == Kind::Hit) {
            to.kind = NEBULA_COOP_WORLD_HIT;
            to.object = from.hit.object;
            to.value = from.hit.message;
            to.scenario = from.hit.sensor;
            to.player = from.hit.player;
        } else if (from.kind == Kind::Switch) {
            to.kind = NEBULA_COOP_WORLD_SWITCH;
            to.object = from.change.id;
            to.value = from.change.on ? 1u : 0u;
        } else if (from.kind == Kind::Clock) {
            to.kind = NEBULA_COOP_WORLD_CLOCK;
            to.value = from.clock_ms;
        } else {
            using galaxy::coop::SaveKind;
            switch (from.save.kind) {
            case SaveKind::Star: to.kind = NEBULA_COOP_WORLD_STAR; break;
            case SaveKind::Flag: to.kind = NEBULA_COOP_WORLD_FLAG; break;
            case SaveKind::Coins: to.kind = NEBULA_COOP_WORLD_COINS; break;
            case SaveKind::RaceTime: to.kind = NEBULA_COOP_WORLD_RACE_TIME; break;
            case SaveKind::StarBits: to.kind = NEBULA_COOP_WORLD_STAR_BITS; break;
            case SaveKind::StarBitsChange: to.kind = NEBULA_COOP_WORLD_STAR_BITS_ADD; break;
            default: to.kind = NEBULA_COOP_WORLD_VALUE; break;
            }
            to.scenario = from.save.scenario;
            to.value = from.save.value;
            if (from.save.kind == SaveKind::StarBitsChange) {
                to.object = from.save.value;
                to.value = 0u;
            }
            std::snprintf(to.name, sizeof(to.name), "%s", from.save.name.c_str());
        }
        waiting.pop_front();
    }
    return count;
}

// Shared objects (galaxy::coop::ObjectState): the ones this game moves this frame, for the others in its instance.
void svc_coop_objects_report(void*, const NebulaCoopObjectState* states, uint32_t count) {
    if (states == nullptr || count == 0u || !gCoopWorld.active()) return;
    std::vector<galaxy::coop::ObjectState> report;
    report.reserve(std::min<uint32_t>(count, 512u));
    for (uint32_t i = 0; i < count && i < 512u; ++i) {
        const NebulaCoopObjectState& from = states[i];
        galaxy::coop::ObjectState& to = report.emplace_back();
        to.object = from.object;
        to.nerve = from.nerve;
        to.step = static_cast<std::uint16_t>(std::min<uint32_t>(from.step, 0xffffu));
        to.event = static_cast<std::uint16_t>(from.event);
        to.flags = static_cast<std::uint8_t>(from.flags & galaxy::coop::kObjectHitNerve);
        std::copy(std::begin(from.position), std::end(from.position), to.position.begin());
        std::copy(std::begin(from.velocity), std::end(from.velocity), to.velocity.begin());
        std::copy(std::begin(from.rotation), std::end(from.rotation), to.rotation.begin());
        to.distance = from.distance;
    }
    gCoopWorld.report_states(report);
}

// The others' objects as they came (the latest of each): what the game can't take this call waits for the next.
uint32_t svc_coop_objects_take(void*, NebulaCoopObjectState* states, uint32_t capacity) {
    std::deque<galaxy::coop::ObjectState>& waiting = gCoopStatesWaiting;
    if (!gCoopWorld.active()) {
        waiting.clear();
        return 0u;
    }
    for (const galaxy::coop::ObjectState& state : gCoopWorld.take_states()) waiting.push_back(state);
    uint32_t count = 0u;
    while (states != nullptr && count < capacity && !waiting.empty()) {
        const galaxy::coop::ObjectState& from = waiting.front();
        NebulaCoopObjectState& to = states[count++];
        to = {};
        to.object = from.object;
        to.nerve = from.nerve;
        to.step = from.step;
        to.flags = from.flags;
        to.event = from.event;
        std::copy(from.position.begin(), from.position.end(), to.position);
        std::copy(from.velocity.begin(), from.velocity.end(), to.velocity);
        std::copy(from.rotation.begin(), from.rotation.end(), to.rotation);
        to.distance = from.distance;
        to.player = from.player;
        waiting.pop_front();
    }
    return count;
}

// The Personal Star Counter (sm64coopdx's mod): the Power Stars this player got in the room, as the host counted them
// (galaxy::coop::World credits each star new to the session's save to who got it).
int32_t svc_coop_star_count(void*) {
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state != galaxy::coop::SessionState::Hosting && state != galaxy::coop::SessionState::Joined) return -1;
    return static_cast<int32_t>(gCoopSession.stars());
}

// An achievement another player unlocked in the room's save, for its pop-up: 1 with its id, 0 when none waits.
int svc_coop_achievement_take(void*, char* id, uint32_t capacity) {
    if (gCoopAchievementsWaiting.empty() || id == nullptr || capacity == 0u) return 0;
    std::snprintf(id, capacity, "%s", gCoopAchievementsWaiting.front().c_str());
    gCoopAchievementsWaiting.pop_front();
    return 1;
}

// The oldest pop-up waiting, for the game's corner boxes: 1 with it, 0 when none waits. Only in a room's game, as the
// chat.
void svc_coop_character_wanted(void*, char* id, uint32_t capacity) {
    if (id == nullptr || capacity == 0u) return;
    const std::lock_guard<std::mutex> lock(gCoopCharacterLock);
    std::snprintf(id, capacity, "%s", gCoopCharacterId.c_str());
}

// Options' colours for the player (its PLAYER page): 1 with them, 0 for their character's own.
int svc_coop_palette_wanted(void*, uint8_t* palette) {
    if (palette == nullptr) return 0;
#if defined(NEBULA_HOST_RENDERER)
    // Live, as the character is (svc_coop_menu): Options' PLAYER page may change them mid-game. The game asks a few
    // times a frame, so the settings are read again at most every quarter second.
    static std::chrono::steady_clock::time_point read{};
    const auto now = std::chrono::steady_clock::now();
    if (!gCoopMenu.is_open() && now - read >= std::chrono::milliseconds(250)) {
        read = now;
        set_coop_character_wanted(galaxy::get_runtime_settings().coop);
    }
#endif
    const std::lock_guard<std::mutex> lock(gCoopCharacterLock);
    if (!gCoopPalette) return 0;
    std::memcpy(palette, gCoopPalette->data(), gCoopPalette->size());
    return 1;
}

int svc_coop_popup_take(void*, NebulaCoopPopup* popup) {
    if (popup == nullptr || !gCoopSession.active()) return 0;
#if defined(NEBULA_HOST_RENDERER)
    if (!galaxy::render::coop_in_game()) return 0;
#endif
    const std::optional<galaxy::coop::Popup> next = gCoopPopups.take(galaxy::coop::chat_clock_ms());
    if (!next) return 0;
    *popup = {};
    using Kind = galaxy::coop::Popup::Kind;
    switch (next->kind) {
    case Kind::Joined: popup->kind = NEBULA_COOP_POPUP_JOINED; break;
    case Kind::Left: popup->kind = NEBULA_COOP_POPUP_LEFT; break;
    case Kind::Observatory: popup->kind = NEBULA_COOP_POPUP_OBSERVATORY; break;
    case Kind::Galaxy: popup->kind = NEBULA_COOP_POPUP_GALAXY; break;
    case Kind::EnteredHere: popup->kind = NEBULA_COOP_POPUP_ENTERED_HERE; break;
    case Kind::LeftHere: popup->kind = NEBULA_COOP_POPUP_LEFT_HERE; break;
    case Kind::Died: popup->kind = NEBULA_COOP_POPUP_DIED; break;
    case Kind::Star: popup->kind = NEBULA_COOP_POPUP_STAR; break;
    }
    popup->character = next->character == galaxy::CoopCharacter::Luigi ? 1u : 0u;
    popup->scenario = next->scenario;
    std::snprintf(popup->name, sizeof(popup->name), "%s", next->name.c_str());
    std::snprintf(popup->stage, sizeof(popup->stage), "%s", next->stage.c_str());
    return 1;
}

// The other players in this player's instance, for their copies: as they were kSnapshotDelay ago, by number. Who
// left the room is forgotten.
uint32_t svc_coop_remote(void*, NebulaCoopRemotePlayer* players, uint32_t capacity) {
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state != galaxy::coop::SessionState::Hosting && state != galaxy::coop::SessionState::Joined) return 0u;
    std::string stage;
    int scenario = 0;
    {
        const std::lock_guard<std::mutex> lock(gCoopLock);
        if (!gCoopLocalValid) return 0u;
        stage = gCoopLocal.stage;
        scenario = gCoopLocal.scenario;
    }
    const std::vector<galaxy::coop::SessionPlayer> room = gCoopSession.players();
    std::vector<galaxy::coop::RemotePlayer> remote = gCoopRemote.sample(stage, scenario);
    std::erase_if(remote, [&](const galaxy::coop::RemotePlayer& player) {
        const bool here = std::any_of(room.begin(), room.end(), [&](const galaxy::coop::SessionPlayer& in) {
            return in.id == player.id && !in.local;
        });
        if (!here) gCoopRemote.remove(player.id);
        return !here;
    });
    // Who died here since the last frame, for the pop-ups.
    std::vector<galaxy::coop::InstancePlayer> instance;
    instance.reserve(remote.size());
    for (const galaxy::coop::RemotePlayer& player : remote) {
        instance.push_back({player.id, player.name, player.character,
                            (player.snapshot.flags & NEBULA_COOP_PLAYER_DEAD) != 0u});
    }
    gCoopPopups.update_instance(instance, galaxy::coop::chat_clock_ms());
    const std::size_t count = std::min<std::size_t>(remote.size(), players != nullptr ? capacity : 0u);
    for (std::size_t i = 0; i < count; ++i) {
        const galaxy::coop::RemotePlayer& from = remote[i];
        const galaxy::coop::PlayerSnapshot& snapshot = from.snapshot;
        NebulaCoopRemotePlayer& to = players[i];
        to = {};
        std::snprintf(to.name, sizeof(to.name), "%s", from.name.c_str());
        to.number = from.index + 1u;
        NebulaCoopPlayer& player = to.player;
        std::snprintf(player.stage, sizeof(player.stage), "%s", snapshot.stage.c_str());
        player.scenario = snapshot.scenario;
        for (std::size_t j = 0; j < 3u; ++j) {
            player.position[j] = snapshot.position[j];
            player.rotation[j] = snapshot.rotation[j];
            player.front[j] = snapshot.front[j];
            player.up[j] = snapshot.up[j];
            player.gravity[j] = snapshot.gravity[j];
            player.velocity[j] = snapshot.velocity[j];
        }
        std::snprintf(player.animation, sizeof(player.animation), "%s", snapshot.animation.c_str());
        std::snprintf(player.group, sizeof(player.group), "%s", snapshot.group.c_str());
        player.animation_frame = snapshot.frame;
        player.animation_rate = snapshot.rate;
        for (std::size_t j = 0; j < 4u; ++j) player.track_weights[j] = snapshot.weights[j];
        for (std::size_t j = 0; j < 4u; ++j) {
            std::snprintf(player.tracks[j], sizeof(player.tracks[j]), "%s", snapshot.tracks[j].c_str());
        }
        player.health = snapshot.health;
        player.mode = snapshot.mode;
        // The character is the one their game plays now (their snapshot's), so a live switch shows at once; a mod's
        // character on it by the id the session last told (the game shows the base until the two agree).
        player.flags = snapshot.flags;
        for (const galaxy::coop::SessionPlayer& in : room) {
            if (in.id != from.id) continue;
            std::snprintf(player.character, sizeof(player.character), "%s", in.character_id.c_str());
            // Their colours, as the session last told them.
            player.has_palette = in.has_palette ? 1u : 0u;
            std::memcpy(player.palette, in.palette.data(), sizeof(player.palette));
        }
        player.area = snapshot.area;
        player.platform = snapshot.platform;
        for (std::size_t j = 0; j < 3u; ++j) {
            player.platform_position[j] = snapshot.platform_position[j];
            player.platform_front[j] = snapshot.platform_front[j];
            player.platform_up[j] = snapshot.platform_up[j];
        }
        // Their snapshot came half a round trip ago and shows kSnapshotDelay late; what they saw of this player had
        // come and shown as late. Two joiners reach each other through the host: each one's ping is to it.
        const auto them = std::find_if(room.begin(), room.end(),
                                       [&](const galaxy::coop::SessionPlayer& in) { return in.id == from.id; });
        const bool both_joiners = !gCoopSession.is_host() && them != room.end() && !them->host;
        const unsigned round_trip = them != room.end() ? them->ping_ms * (both_joiners ? 2u : 1u) : 0u;
        to.rewind_ms = round_trip + 2u * static_cast<unsigned>(galaxy::coop::kSnapshotDelay.count());
    }
    return static_cast<uint32_t>(remote.size());
}

// The room's player collision options, the host's as every player has them.
void svc_coop_rules(void*, NebulaCoopRules* rules) {
    if (rules == nullptr) return;
    *rules = {};
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state != galaxy::coop::SessionState::Hosting && state != galaxy::coop::SessionState::Joined) return;
    const galaxy::RuntimeCoopSettings room = gCoopSession.room();
    static_assert(static_cast<unsigned>(galaxy::CoopPlayerInteraction::Solid) == NEBULA_COOP_INTERACTION_SOLID &&
                  static_cast<unsigned>(galaxy::CoopPlayerInteraction::FriendlyFire) == NEBULA_COOP_INTERACTION_FRIENDLY_FIRE &&
                  static_cast<unsigned>(galaxy::CoopPvpMode::Revamped) == NEBULA_COOP_PVP_REVAMPED);
    rules->interaction = static_cast<uint32_t>(room.player_interaction);
    rules->knockback = room.knockback_strength;
    rules->pvp = static_cast<uint32_t>(room.pvp_mode);
    rules->bubble = room.bubble_death ? 1u : 0u;
}

// Nebula's screens with small text (sharp_2d): the 2D layer at the window's resolution while they show.
void svc_sharp_2d(void*, uint32_t on) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::request_sharp_2d(on != 0u);
#else
    (void)on;
#endif
}

void svc_movie_frame(void*, const char* name, int32_t frame, float frame_rate, const void* data, uint64_t size,
                     uint32_t width, uint32_t height) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::attach_movie_frame(name, frame, frame_rate, data, size, width, height);
#else
    (void)name, (void)frame, (void)frame_rate, (void)data, (void)size, (void)width, (void)height;
#endif
}

// The game's steady clock: nanoseconds since the process's first read, plus one (0 means "never" to its users).
uint64_t svc_clock_ns(void*) {
    if (deterministic_mode()) {
        return gDeterministicFrames.load(std::memory_order_relaxed) * 1000000000ull / static_cast<uint64_t>(galaxy::kGameStepsPerSecond) + 1u;
    }
    static const Clock::time_point start = Clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()) + 1u;
}

int64_t svc_calendar_ns(void*) {
    if (deterministic_mode()) {
        // A fixed date (2026-01-01 from 2000-01-01), then the frames' time.
        constexpr int64_t kFixedDate = 820454400;
        return kFixedDate * 1000000000ll + static_cast<int64_t>(gDeterministicFrames.load(std::memory_order_relaxed)) * galaxy::kGameStepNs;
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
    const std::time_t fields = _mkgmtime(&local);
#else
    localtime_r(&seconds, &local);
    const std::time_t fields = timegm(&local);
#endif
    const int64_t unix_to_2000 = 946684800;
    const int64_t fraction = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() %
        1000000000;
    return (static_cast<int64_t>(fields) - unix_to_2000) * 1000000000 + fraction;
}

std::atomic<int32_t> gStarPointerMode{-1};

void svc_button_icons(void*, NebulaButtonIcons* icons) {
    *icons = NebulaButtonIcons{};
#if defined(NEBULA_HOST_RENDERER)
    static_assert(galaxy::input::kButtonIconCount == NEBULA_BUTTON_ICONS &&
                  static_cast<int>(galaxy::input::ButtonIconStyle::Keyboard) == NEBULA_ICON_STYLE_KEYBOARD &&
                  static_cast<int>(galaxy::input::ButtonGlyph::Key) == NEBULA_ICON_KEY &&
                  static_cast<int>(galaxy::input::ButtonGlyph::StickAxis) == NEBULA_ICON_STICK_AXIS &&
                  static_cast<int>(galaxy::input::ButtonIconModel::GameCube) == NEBULA_ICON_MODEL_GAMECUBE &&
                  static_cast<int>(galaxy::input::ButtonIconModel::Steam) == NEBULA_ICON_MODEL_STEAM &&
                  static_cast<int>(galaxy::input::ButtonIconModel::DualSenseEdge) == NEBULA_ICON_MODEL_DUALSENSE_EDGE &&
                  galaxy::input::kKeyIconLayoutFirst == NEBULA_KEY_LAYOUT_FIRST &&
                  galaxy::input::kKeyIconLayoutCount == NEBULA_KEY_LAYOUT_KEYS &&
                  galaxy::input::kKeyIconMouseLeft == NEBULA_KEY_MOUSE_LEFT &&
                  galaxy::input::kKeyIconOther == NEBULA_KEY_OTHER && galaxy::input::kKeyIconCount == NEBULA_KEYS &&
                  NEBULA_KEY_SPACE == 75 && NEBULA_KEY_RIGHT_WIN == 106 && NEBULA_KEYS < galaxy::input::kKeyIconNone);
    const int32_t mode = gStarPointerMode.load(std::memory_order_relaxed);
    const bool menu = mode >= 0 && static_cast<uint32_t>(mode) < galaxy::input::kStarPointerModeCount &&
                      galaxy::input::play_context_from_star_pointer_mode(static_cast<uint32_t>(mode)) !=
                          galaxy::input::PlayContext::Play;
    const galaxy::input::ButtonIconSpec spec = galaxy::host::button_icons(menu);
    icons->style = static_cast<uint8_t>(spec.style);
    icons->model = static_cast<uint8_t>(spec.model);
    for (size_t i = 0; i < NEBULA_BUTTON_ICONS; ++i) {
        icons->glyphs[i] = static_cast<uint8_t>(spec.glyphs[i]);
        icons->places[i] = spec.places[i];
    }
    for (size_t i = 0; i < 4u; ++i) icons->pad_keys[i] = spec.pad_keys[i];
    for (size_t i = 0; i < NEBULA_KEY_LAYOUT_KEYS; ++i) icons->layout[i] = spec.layout[i];
    for (size_t i = 0; i < NEBULA_BUTTON_ICONS; ++i) {
        std::memcpy(icons->other[i], spec.other[i].data(), NEBULA_KEY_NAME_CHARS);
        icons->other[i][NEBULA_KEY_NAME_CHARS - 1] = '\0';
    }
#endif
#if defined(NEBULA_REAL_WIIMOTE)
    icons->wii_remote = galaxy::host::real_wiimote_in_use() ? 1u : 0u;
#endif
}

// The scale the renderer last drew the 2D layer at (render_core: the scene target in use when the scene ends).
uint32_t svc_layer_2d_lines(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::layer_2d_lines();
#else
    return 0u;
#endif
}

// The game asks while it could pause: 1 once per request.
int svc_take_pause_request(void*) {
    return gPauseRequest.exchange(false, std::memory_order_acq_rel) ? 1 : 0;
}

#if defined(NEBULA_HOST_RENDERER)
// The settings' galaxy select: its galaxies for the game to name, and the names it gives.
int svc_galaxy_select_stage(void*, uint32_t index, char* id, uint32_t capacity, uint32_t* stars) {
    if (index >= galaxy::render::kGalaxyStageCount || id == nullptr || capacity == 0u) {
        return 0;
    }
    std::snprintf(id, capacity, "%s", galaxy::render::kGalaxyStages[index].id);
    if (stars != nullptr) *stars = galaxy::render::kGalaxyStages[index].stars;
    return 1;
}

int svc_galaxy_select_names_wanted(void*) {
    return galaxy::render::take_galaxy_select_names_wanted() ? 1 : 0;
}

void svc_galaxy_select_name(void*, uint32_t index, uint32_t star, const uint16_t* name) {
    galaxy::render::set_galaxy_select_name(index, star, message_utf8(name));
}
#else
int svc_galaxy_select_stage(void*, uint32_t, char*, uint32_t, uint32_t*) { return 0; }
int svc_galaxy_select_names_wanted(void*) { return 0; }
void svc_galaxy_select_name(void*, uint32_t, uint32_t, const uint16_t*) {}
#endif

// What the game draws from, registered once. A service reads what it is given before it returns.
#if defined(NEBULA_HOST_RENDERER)
uint32_t svc_model_register(void*, const void* file, uint32_t* first_mesh) {
    return galaxy::render::registries().model_register(file, first_mesh);
}
void svc_model_free(void*, uint32_t model) {
    galaxy::render::registries().model_free(model);
}
uint32_t svc_mesh_register(void*, const NebulaFrameMesh* mesh) {
    return galaxy::render::registries().mesh_register(*mesh);
}
void svc_mesh_free(void*, uint32_t mesh) {
    galaxy::render::registries().mesh_free(mesh);
}
uint32_t svc_texture_register(void*, const NebulaTextureImage* image) {
    return galaxy::render::textures().register_image(*image);
}
void svc_texture_update(void*, uint32_t texture, const void* bytes, uint32_t size) {
    galaxy::render::textures().update_image(texture, bytes, size);
}
void svc_texture_free(void*, uint32_t texture) {
    galaxy::render::textures().free_image(texture);
}
uint32_t svc_texture_target(void*) {
    return galaxy::render::textures().new_target();
}
uint32_t svc_texture_table(void*, const NebulaFrameTexture* list, uint32_t count) {
    return galaxy::render::registries().texture_table(list, count);
}
uint32_t svc_material_register(void*, const float (*rows)[4], uint32_t vertex_rows, uint32_t pixel_rows) {
    return galaxy::render::registries().material_register(rows, vertex_rows, pixel_rows);
}
uint32_t svc_pipeline_register(void*, const NebulaPipelineDesc* desc, const NebulaLayoutInput* inputs) {
    return galaxy::render::registries().pipeline_register(*desc, inputs);
}
void svc_texture_hd(void*, uint32_t texture, const void* hd) {
    galaxy::render::textures().set_built_in(texture, hd);
}
void svc_submit_frame(void*, NebulaFrameList* list) {
    if ((list->flags & NEBULA_FRAME_HAS_SCENE) != 0u) {
        // The first 3D scene (the title's, or a test's galaxy) is where the game first shows a menu: one the boot's
        // close hid with a question standing opens again (svc_settings_input). An extension preparing the game unseen
        // (a test's setup, which needs the scene and its sequences going) holds it until it is done.
        gSceneBegins.fetch_add(1, std::memory_order_relaxed);
        if (!galaxy::extensions::holds_menus()) galaxy::render::native_settings_reopen_held();
        end_boot_fast("first 3D scene");
    }
    list->game_frame = gGameFrames.load(std::memory_order_relaxed);
    galaxy::render::attach_frame_list(list);
    submit_frame(false, false);
}
uint32_t svc_probe_results(void*, uint32_t channel, NebulaScreenProbe* probes, uint32_t capacity) {
    static_assert(sizeof(NebulaScreenProbe) == sizeof(galaxy::render::ScreenProbe));
    return galaxy::render::screen_probe_results(channel, reinterpret_cast<galaxy::render::ScreenProbe*>(probes), capacity);
}
// draw.post (docs/modding/reference/services/draw.md): the program compiled for the running renderer (mod_post.cpp).
static_assert(int{galaxy::render::kModPostOk} == NEBULA_POST_OK && int{galaxy::render::kModPostCompile} == NEBULA_POST_COMPILE &&
                  int{galaxy::render::kModPostNoMotion} == NEBULA_POST_NO_MOTION,
              "post_register's results are the game's");
int32_t svc_post_register(void*, const void* text, uint32_t size, uint32_t reads, uint32_t* program, char* error,
                          uint32_t error_capacity) {
    return galaxy::render::mod_post_register(text, size, reads, program, error, error_capacity);
}
void svc_post_free(void*, uint32_t program) {
    galaxy::render::mod_post_free(program);
}
#else
// With no renderer nothing is drawn: every id is 0 and a list is read by nobody.
uint32_t svc_model_register(void*, const void*, uint32_t* first_mesh) {
    *first_mesh = 0u;
    return 0u;
}
void svc_model_free(void*, uint32_t) {}
uint32_t svc_mesh_register(void*, const NebulaFrameMesh*) {
    return 0u;
}
void svc_mesh_free(void*, uint32_t) {}
uint32_t svc_texture_register(void*, const NebulaTextureImage*) {
    return 0u;
}
void svc_texture_update(void*, uint32_t, const void*, uint32_t) {}
void svc_texture_free(void*, uint32_t) {}
uint32_t svc_texture_target(void*) {
    return 0u;
}
uint32_t svc_texture_table(void*, const NebulaFrameTexture*, uint32_t) {
    return 0u;
}
uint32_t svc_material_register(void*, const float (*)[4], uint32_t, uint32_t) {
    return 0u;
}
uint32_t svc_pipeline_register(void*, const NebulaPipelineDesc*, const NebulaLayoutInput*) {
    return 0u;
}
void svc_texture_hd(void*, uint32_t, const void*) {}
void svc_submit_frame(void*, NebulaFrameList* list) {
    if ((list->flags & NEBULA_FRAME_HAS_SCENE) != 0u) {
        gSceneBegins.fetch_add(1, std::memory_order_relaxed);
        end_boot_fast("first 3D scene");
    }
    list->released = 1u;
}
uint32_t svc_probe_results(void*, uint32_t, NebulaScreenProbe*, uint32_t) {
    return 0u;
}
int32_t svc_post_register(void*, const void*, uint32_t, uint32_t, uint32_t* program, char* error, uint32_t error_capacity) {
    if (program != nullptr) {
        *program = 0u;
    }
    if (error != nullptr && error_capacity != 0u) {
        std::snprintf(error, error_capacity, "%s", "no renderer: nothing is drawn");
    }
    return NEBULA_POST_COMPILE;
}
void svc_post_free(void*, uint32_t) {}
#endif

// The Mii library's file (mii_import.h) in the data folder; a Wii save folder's Mii database (or an earlier Nebula's)
// is where it starts the first time. The game reads its Miis by place as typed records.
fs::path mii_library_path() {
    return gSaveRoot / "nebula-miis.txt";
}
fs::path console_mii_database_path() {
    return gSaveRoot / fs::path(galaxy::host::wii_mii_database_member());
}

NebulaMii nebula_mii(const galaxy::host::MiiFields& f) {
    NebulaMii m{};
    m.sex = f.sex;
    m.birth_month = f.birth_month;
    m.birth_day = f.birth_day;
    m.favorite_color = f.favorite_color;
    m.favorite = f.favorite;
    m.local_only = f.local_only;
    m.height = f.height;
    m.build = f.build;
    m.face_type = f.face_type;
    m.face_color = f.face_color;
    m.face_texture = f.face_texture;
    m.hair_type = f.hair_type;
    m.hair_color = f.hair_color;
    m.hair_flip = f.hair_flip;
    m.eye_type = f.eye_type;
    m.eye_color = f.eye_color;
    m.eye_scale = f.eye_scale;
    m.eye_rotate = f.eye_rotate;
    m.eye_x = f.eye_x;
    m.eye_y = f.eye_y;
    m.eyebrow_type = f.eyebrow_type;
    m.eyebrow_color = f.eyebrow_color;
    m.eyebrow_scale = f.eyebrow_scale;
    m.eyebrow_rotate = f.eyebrow_rotate;
    m.eyebrow_x = f.eyebrow_x;
    m.eyebrow_y = f.eyebrow_y;
    m.nose_type = f.nose_type;
    m.nose_scale = f.nose_scale;
    m.nose_y = f.nose_y;
    m.mouth_type = f.mouth_type;
    m.mouth_color = f.mouth_color;
    m.mouth_scale = f.mouth_scale;
    m.mouth_y = f.mouth_y;
    m.mustache = f.mustache;
    m.beard_type = f.beard_type;
    m.beard_color = f.beard_color;
    m.beard_scale = f.beard_scale;
    m.beard_y = f.beard_y;
    m.glass_type = f.glass_type;
    m.glass_color = f.glass_color;
    m.glass_scale = f.glass_scale;
    m.glass_y = f.glass_y;
    m.mole_type = f.mole_type;
    m.mole_scale = f.mole_scale;
    m.mole_x = f.mole_x;
    m.mole_y = f.mole_y;
    std::copy(f.name.begin(), f.name.end(), m.name);
    std::copy(f.creator.begin(), f.creator.end(), m.creator);
    std::copy(f.create_id.begin(), f.create_id.end(), m.create_id);
    return m;
}

uint32_t svc_mii_count(void*) {
    return galaxy::host::mii_library_count();
}

int32_t svc_mii_record(void*, uint32_t place, NebulaMii* mii) {
    const std::optional<galaxy::host::MiiFields> fields = galaxy::host::mii_library_mii(place);
    if (!fields) {
        return 0;
    }
    if (mii != nullptr) {
        *mii = nebula_mii(*fields);
    }
    return 1;
}

// The wheel's turn the window gathered since the game last asked.
int32_t svc_pointer_wheel(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::take_pointer_wheel();
#else
    return 0;
#endif
}

std::atomic<uint32_t> gTiltSection{0};
std::atomic<uint8_t> gMotor{0};

// The game's thread only: the pointer mode the controls follow, held while a closed wipe covers the screen.
galaxy::input::CoveredModeHold gModeHold;

void follow_star_pointer_mode(int32_t mode) {
    char text[64];
    if (gStarPointerMode.exchange(mode, std::memory_order_relaxed) != mode) {
        std::snprintf(text, sizeof(text), "star pointer mode %d", static_cast<int>(mode));
        log_line("context:", text);
    }
}

void svc_game_context(void*, uint32_t what, int32_t value) {
    char text[64];
    if (what == NEBULA_CONTEXT_STAR_POINTER_MODE) {
        const bool was_holding = gModeHold.holding();
        follow_star_pointer_mode(gModeHold.game(value));
        if (gModeHold.holding() && !was_holding) {
            std::snprintf(text, sizeof(text), "star pointer mode %d held under the wipe", static_cast<int>(value));
            log_line("context:", text);
        }
    } else if (what == NEBULA_CONTEXT_SCREEN_COVERED) {
        follow_star_pointer_mode(gModeHold.covered(value != 0));
    } else if (what == NEBULA_CONTEXT_TILT_SECTION) {
        if (gTiltSection.exchange(static_cast<uint32_t>(value), std::memory_order_relaxed) != static_cast<uint32_t>(value)) {
            std::snprintf(text, sizeof(text), "tilt section %d", static_cast<int>(value));
            log_line("context:", text);
        }
    } else if (what == NEBULA_CONTEXT_FRAME_GENERATION) {
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::note_frame_generation_allowed(value != 0);
#endif
    } else if (what == NEBULA_CONTEXT_CAMERA_WATER) {
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::note_camera_water(value > 0 ? static_cast<float>(value - 1) : -1.0f);
#endif
    } else if (what == NEBULA_CONTEXT_TOUCH_TARGET) {
#if defined(NEBULA_HOST_RENDERER)
        galaxy::host::note_touch_target(static_cast<std::uint32_t>(value));
#endif
    } else if (what == NEBULA_CONTEXT_CORNER_BUTTONS) {
#if defined(NEBULA_HOST_RENDERER)
        galaxy::host::note_corner_buttons(static_cast<std::uint32_t>(value));
#endif
    } else if (what == NEBULA_CONTEXT_SCENE_PLAY) {
#if defined(NEBULA_HOST_RENDERER)
        galaxy::calibration_scene::note_play(value != 0);
        galaxy::bench::note_play(value != 0);
#endif
    }
}

const galaxy::assets::AssetStore& asset_table() {
    return gPak.store;
}

#if defined(NEBULA_HOST_RENDERER)
// Ray tracing's tables in the pak (nebula-import: each stage's numbers, the particle effects' light), read once before
// the renderer starts; a pak without them leaves the defaults.
void load_rt_tables() {
    const auto text_of = [](const char* path) {
        std::string text;
        const std::optional<galaxy::assets::Entry> entry = asset_table().find(path);
        if (entry && !entry->folder) {
            text.resize(static_cast<std::size_t>(entry->length));
            if (!gPak.store.read(entry->position, text.size(), text.data())) {
                text.clear();
            }
        }
        return text;
    };
    galaxy::render::set_rt_tables(text_of(galaxy::render::kRtStagesPath), text_of(galaxy::render::kRtEffectsPath));
}
#endif

int asset_entry_out(const std::optional<galaxy::assets::Entry>& entry, NebulaAssetEntry* out) {
    if (!entry) {
        return 0;
    }
    *out = {};
    out->index = entry->index;
    out->length = static_cast<uint32_t>(entry->length);
    out->position = entry->position;
    out->children = entry->children;
    out->folder = entry->folder ? 1 : 0;
    out->kind = entry->kind;
    out->name = entry->name;
    return 1;
}

// A file a mod provides (assets 1.1, luma_assets_service.h) comes before the store's at its path; its index, above
// every store's, leads entry, child, map and read to it.
int svc_asset_find(void*, const char* path, NebulaAssetEntry* out) {
    if (path == nullptr) {
        return 0;
    }
    if (std::optional<galaxy::assets::Entry> provided = galaxy::luma_assets::provided_find(path)) {
        return asset_entry_out(provided, out);
    }
    return asset_entry_out(asset_table().find(path), out);
}

int svc_asset_find_game(void*, const char* path, NebulaAssetEntry* out) {
    return path != nullptr ? asset_entry_out(asset_table().find_game(path), out) : 0;
}

int svc_asset_entry(void*, int32_t index, NebulaAssetEntry* out) {
    return asset_entry_out(galaxy::luma_assets::is_provided(index) ? galaxy::luma_assets::provided_entry(index)
                                                                   : asset_table().entry(index),
                           out);
}

int svc_asset_added_child(void*, const char* path, uint32_t child, NebulaAssetEntry* out) {
    // What a mod provides at the path replaces the game's whole: nothing is added to it.
    if (path == nullptr || galaxy::luma_assets::provided_find(path)) {
        return 0;
    }
    return asset_entry_out(asset_table().added_child(path, child), out);
}

int svc_asset_child(void*, int32_t folder, uint32_t child, NebulaAssetEntry* out) {
    return asset_entry_out(galaxy::luma_assets::is_provided(folder) ? galaxy::luma_assets::provided_child(folder, child)
                                                                    : asset_table().child(folder, child),
                           out);
}

// The game's memory. An arena is reserved from the top of the address space down, far above 4 GB and the file views, so an
// address squeezed through 32 bits fails at once, in every run.
void* svc_memory_reserve(void*, uint64_t size) {
#if defined(_WIN32)
    return VirtualAlloc(nullptr, static_cast<SIZE_T>(size), MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS);
#else
    void* const block = mmap(nullptr, static_cast<size_t>(size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return block != MAP_FAILED ? block : nullptr;
#endif
}

int svc_memory_commit(void*, void* start, uint64_t size) {
#if defined(_WIN32)
    return VirtualAlloc(start, static_cast<SIZE_T>(size), MEM_COMMIT, PAGE_READWRITE) != nullptr ? 1 : 0;
#else
    (void)start, (void)size;
    return 1;
#endif
}

void svc_memory_decommit(void*, void* start, uint64_t size) {
#if defined(_WIN32)
    VirtualFree(start, static_cast<SIZE_T>(size), MEM_DECOMMIT);
#else
    madvise(start, static_cast<size_t>(size), MADV_DONTNEED);
#endif
}

void svc_memory_release(void*, void* start, uint64_t size) {
#if defined(_WIN32)
    (void)size;
    VirtualFree(start, 0, MEM_RELEASE);
#else
    munmap(start, static_cast<size_t>(size));
#endif
}

// An extension's slow drive (disc_read_delay_us): each way to the store takes this long, so waits on loads are exercised.
void test_drive_delay() {
    const uint32_t delay_us = galaxy::extensions::disc_read_delay_us();
    if (delay_us != 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }
}

const void* svc_asset_map(void*, int32_t file) {
    test_drive_delay();
    if (galaxy::luma_assets::is_provided(file)) {
        return galaxy::luma_assets::provided_view(file);
    }
    const std::optional<galaxy::assets::Entry> entry = asset_table().entry(file);
    if (!entry) {
        return nullptr;
    }
    return gPak.store.map(entry->position, static_cast<std::size_t>(entry->length));
}

void* svc_asset_map_private(void*, int32_t file) {
    test_drive_delay();
    // A provided file is no view of the store: the game reads it into its own memory instead (GameAssets::load).
    if (galaxy::luma_assets::is_provided(file)) {
        return nullptr;
    }
    const std::optional<galaxy::assets::Entry> entry = asset_table().entry(file);
    if (!entry) {
        return nullptr;
    }
    void* const data = gPak.store.map_private(entry->position, static_cast<std::size_t>(entry->length));
    const bool trace_reads = ::galaxy::diagnosing(::galaxy::kDiagnoseFileReads);
    if (trace_reads) {
        std::fprintf(stderr, "[read] frame %llu entry %d mapped size %llu %s at %p\n", static_cast<unsigned long long>(gDeterministicFrames.load()), file,
                     static_cast<unsigned long long>(entry->length), entry->name != nullptr ? entry->name : "?", data);
    }
    return data;
}

void svc_asset_unmap(void*, void* data, uint64_t size) {
    const bool trace_reads = ::galaxy::diagnosing(::galaxy::kDiagnoseFileReads);
    if (trace_reads) {
        std::fprintf(stderr, "[read] frame %llu unmapped %p size %llu\n", static_cast<unsigned long long>(gDeterministicFrames.load()), data, static_cast<unsigned long long>(size));
    }
    gPak.store.unmap_private(data, static_cast<std::size_t>(size));
}

int32_t svc_asset_read(void*, int32_t file, uint64_t file_offset, uint32_t size, void* dst) {
    test_drive_delay();
    gFileReads.fetch_add(1, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
    galaxy::bench::note_read(size);  // a benchmark's read count (benchmark.h); only the renderer hosts run one
#endif
    // --diagnostics file-reads: each read with the frame it came on: two runs of deterministic mode compare by it.
    const bool trace_reads = ::galaxy::diagnosing(::galaxy::kDiagnoseFileReads);
    const bool provided = galaxy::luma_assets::is_provided(file);
    const std::optional<galaxy::assets::Entry> entry =
        provided ? galaxy::luma_assets::provided_entry(file) : asset_table().entry(file);
    if (trace_reads) {
        std::fprintf(stderr, "[read] frame %llu entry %d at %llu size %u %s\n", static_cast<unsigned long long>(gDeterministicFrames.load(std::memory_order_relaxed)),
                     file, static_cast<unsigned long long>(file_offset), size, entry && entry->name != nullptr ? entry->name : "?");
    }
    if (provided) {
        return galaxy::luma_assets::provided_read(file, file_offset, size, dst) ? static_cast<int32_t>(size) : -1;
    }
    if (!entry || file_offset > entry->length || entry->length - file_offset < size ||
        !gPak.store.read(entry->position + file_offset, size, dst)) {
        return -1;
    }
    return static_cast<int32_t>(size);
}

const char* svc_game_id(void*) {
    return gPak.id;
}

void svc_frame_begun(void*, int black, uint32_t frame) {
    gGameFrames.store(frame, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
    if (const uint32_t at = gResultsFrame.load(std::memory_order_acquire); at != 0u && frame >= at) {
        gResultsFrame.store(0u, std::memory_order_relaxed);
        std::filesystem::path result;
        {
            const std::lock_guard<std::mutex> lock(gResultsMutex);
            result.swap(gResultsPending);
        }
        log_line("host:", "the benchmark's results open");
        galaxy::render::host_window::open_native_settings_results(result);
    }
#endif
#if defined(NEBULA_HOST_RENDERER)
    // --diagnostics frame-times: each frame's time, where a scripted press begins (press-to-present measurements).
    if (::galaxy::diagnosing(::galaxy::kDiagnoseFrameTimes)) std::fprintf(stderr, "[latency] frame=%u qpc=%lld\n", frame, static_cast<long long>(galaxy::qpc_now()));
#endif
#if defined(NEBULA_HOST_RENDERER)
#if defined(_WIN32)
    galaxy::timeline::record(galaxy::timeline::Event::GameFrame, frame, 0u);
#endif
    if (frame >= kBootFrames) {
        end_boot_fast("start's frames run out");
    }
#endif
    galaxy::extensions::frame({frame});
    // Dev mode's reloads (reload.md 4): a request looked for every 30 frames, a reload's steps; nothing without --mod-dev.
    galaxy::mods::dev_frame();
    // The Mods screens' answers to mods' requests (galaxy/mod_screens.h), on the game thread at the frame's start.
    galaxy::mods::run_screen_answers();
#if defined(NEBULA_HOST_RENDERER)
    {
        // While the start runs ahead, one frame a display period goes to the screen.
        bool present = true;
        if (gBootFast.load(std::memory_order_relaxed) && !deterministic_mode()) {
            static Clock::time_point shown{};
            const Clock::time_point now = Clock::now();
            present = now - shown >= std::chrono::milliseconds(16);
            if (present) shown = now;
        }
        // The renderer's newest picture, or black while the game shows black.
        submit_frame(present, black != 0);
    }
    // Miis imported from the settings while the game runs: the game reads the library again.
    if (const auto placed = galaxy::host::take_mii_installs(); !placed.empty()) {
        gModule.mii_changed();
        std::fprintf(stderr, "[mii] %zu imported while running\n", placed.size());
    }
    // The output's shape (the window's, or the chosen aspect) is latched once this frame's picture is presented:
    // queued frames finish first, so a picture is never fitted with the next frame's camera and layouts.
    {
        const auto client = galaxy::render::aspect_latch_extent();
        if (client.width > 1 && client.height > 1 &&
            galaxy::display_aspect_change_pending(client.width, client.height)) {
            galaxy::render::wait_for_render_idle();
            const auto settled = galaxy::render::aspect_latch_extent();
            if (settled.width > 1 && settled.height > 1) {
                galaxy::latch_display_aspect(settled.width, settled.height);
            }
        }
    }
#else
    (void)black;
#endif
}

// The shape the game draws for (display_native.c turns it into the game's screen width and camera aspect): the
// latched output shape; headless, 16:9.
static_assert(galaxy::kNarrowestContentAspect == NEBULA_DISPLAY_NARROWEST_ASPECT &&
              galaxy::kWidestContentAspect == NEBULA_DISPLAY_WIDEST_ASPECT, "host and game clamp to one range");
uint32_t svc_display_extent(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return static_cast<uint32_t>(galaxy::g_display_aspect_word.load(std::memory_order_acquire));
#else
    return 0;
#endif
}

// While their files cook at the start: the phase NEBULA_OPTION_PREPARATION reports, and where the cooker says how far it is.
std::atomic<uint32_t> gModsPreparing{0u};
#if defined(NEBULA_HOST_RENDERER)
bool gPipelinesPrepared = false;
#endif
fs::path gModsProgressFile;

#if defined(NEBULA_HOST_RENDERER)
// NEBULA_OPTION_SCENE_TEXTURES: a loaded scene waits for the texture packs' replacements of the textures it made. They are
// the texture operations queued when it first asks; it waits until the render thread has applied them and none of their
// replacements is loading or uploading, at most kSceneTexturesMostMs. Not with texture packs off, nor in deterministic
// mode (the frames waited would be the machine's).
struct SceneTexturesWait {
    bool waiting = false;
    uint64_t ops = 0u;
    std::chrono::steady_clock::time_point since{};
};
SceneTexturesWait gSceneTexturesWait;
constexpr int64_t kSceneTexturesMostMs = 20000;

bool scene_textures_wait() {
    galaxy::render::TextureStore& store = galaxy::render::textures();
    SceneTexturesWait& wait = gSceneTexturesWait;
    if (deterministic_mode() || !galaxy::render::get_render_config().texture_packs) {
        store.set_replacements_hurried(false);
        wait.waiting = false;
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!wait.waiting) {
        wait.waiting = true;
        wait.ops = store.queued_ops();
        wait.since = now;
    }
    const int64_t waited = std::chrono::duration_cast<std::chrono::milliseconds>(now - wait.since).count();
    const bool ready = store.applied_ops() >= wait.ops && store.replacements_coming() == 0u;
    if (!ready && waited < kSceneTexturesMostMs) {
        store.set_replacements_hurried(true);
        return true;
    }
    store.set_replacements_hurried(false);
    wait.waiting = false;
    if (!ready) {
        const std::string text = "the scene starts with replacements still loading (" + std::to_string(waited) + " ms)";
        log_line("texture-pack:", text.c_str());
    } else if (waited >= 100) {
        const std::string text = "the scene waited " + std::to_string(waited) + " ms for its replacements";
        log_line("texture-pack:", text.c_str());
    }
    return false;
}
#endif

uint32_t svc_option(void*, uint32_t what) {
    switch (what) {
    case NEBULA_OPTION_TEXT_SPEED:
#if defined(NEBULA_HOST_RENDERER)
        return std::clamp(galaxy::get_runtime_settings().text_speed, 1u, 4u);
#else
        return 1;
#endif
    case NEBULA_OPTION_PIXEL_LIT:
#if defined(NEBULA_HOST_RENDERER)
    {
        const galaxy::render::RenderConfig render = galaxy::render::get_render_config();
        return galaxy::render::lights_at_pixel(render) ? 1u : 0u;
    }
#else
        return 0;
#endif
    case NEBULA_OPTION_INPUT_MODE:
#if defined(NEBULA_HOST_RENDERER)
        switch (galaxy::get_runtime_input_mode_state().mode) {
        case galaxy::RuntimeInputMode::Controller: return NEBULA_INPUT_MODE_CONTROLLER;
        case galaxy::RuntimeInputMode::RealWiimote:
        case galaxy::RuntimeInputMode::WiimoteMotion: return NEBULA_INPUT_MODE_WII_REMOTE;
        case galaxy::RuntimeInputMode::Phone:
        case galaxy::RuntimeInputMode::Auto: return NEBULA_INPUT_MODE_AUTO;
        case galaxy::RuntimeInputMode::KeyboardMouse: break;
        }
#endif
        return NEBULA_INPUT_MODE_KEYBOARD_MOUSE;
    case NEBULA_OPTION_MENU_TEXT_SIZE:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::get_runtime_settings().menu_text_size;
#else
        return 0;
#endif
    case NEBULA_OPTION_MODS_CHANGED:
        if (galaxy::extensions::mods_changed()) {
            return 1u;
        }
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::render::native_settings_mods_changed() ? 1u : 0u;
#else
        return 0;
#endif
    case NEBULA_OPTION_WINDOW_WIDTH_DP:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::host::window_width_dp();
#else
        return 0;
#endif
    case NEBULA_OPTION_BLOOM:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::render::bloom_route();
#else
        return 0;
#endif
    case NEBULA_OPTION_PREPARE_KIND:
        return gModsPreparing.load(std::memory_order_acquire) != 0u ? 1u : 2u;
    case NEBULA_OPTION_PREPARATION: {
        const uint32_t mods = gModsPreparing.load(std::memory_order_acquire);
        if (mods != 0u) {
            return mods;
        }
#if defined(NEBULA_HOST_RENDERER)
        // The title waits once for the pipeline list; what a later settings change adds prepares behind the game.
        // Deterministic test mode doesn't: how long the workers take is the machine's, so the frames waited would differ
        // run to run, and a draw makes its own pipeline when it isn't made yet.
        if (!gPipelinesPrepared) {
            gPipelinesPrepared = deterministic_mode() || galaxy::render::pipeline_progress().left == 0u;
        }
        return gPipelinesPrepared ? 0u : 2u;
#else
        return 0u;
#endif
    }
    case NEBULA_OPTION_PREPARE_PROGRESS: {
        if (gModsPreparing.load(std::memory_order_acquire) == 0u) {
#if defined(NEBULA_HOST_RENDERER)
            const galaxy::render::PipelineProgress progress = galaxy::render::pipeline_progress();
            const uint64_t all = uint64_t{progress.done} + progress.left;
            return all != 0u ? static_cast<uint32_t>(progress.done * uint64_t{10000u} / all) : 0u;
#else
            return 0u;
#endif
        }
        std::ifstream in(gModsProgressFile);
        uint64_t done = 0;
        uint64_t total = 0;
        in >> done >> total;
        return total != 0u ? static_cast<uint32_t>(done * 10000u / total) : 0u;
    }
    case NEBULA_OPTION_DIAGNOSE_SCENE_LOADS:
        return ::galaxy::diagnosing(::galaxy::kDiagnoseSceneLoads) ? 1u : 0u;
    case NEBULA_OPTION_TOUCH_POINTER:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::host::touch_pointer_state();
#else
        return 0;
#endif
    case NEBULA_OPTION_GRAPHICS_BACKEND:
        // draw.native's LumaDrawNativeBackend: the renderer this program was built with (one a session). Metal has
        // none yet (0: draw.native's functions are not called).
#if defined(NEBULA_HOST_RENDERER) && defined(NEBULA_HOST_D3D12)
        return 1u;
#elif defined(NEBULA_HOST_RENDERER) && defined(NEBULA_GPU_D3D11)
        return 3u;
#elif defined(NEBULA_HOST_RENDERER) && !defined(__APPLE__)
        return 2u;
#else
        return 0u;
#endif
    case NEBULA_OPTION_RAY_TRACING:
#if defined(NEBULA_HOST_RENDERER)
    {
        if (!galaxy::render::get_render_config().ray_tracing_enabled || !galaxy::render::rt_capable(galaxy::render::rt_caps())) {
            return 0u;
        }
        // A stage's row may keep its shadow light sets as the artists made them (rt_stages.tsv `shadow sets`).
        const galaxy::render::RtStage* stage = galaxy::render::rt_stage(galaxy::render::rt_stage_hash(gSceneStage));
        // Reflections traced at High show what a mirror shows: the game's mirror pass is not drawn.
        // The reflections pass's own rule (rt_pass_reflect.cpp rt_pass_reflect_on): not under the reference
        // (its preset or rt-reference) nor rt-added or rt-motion, which leave the pass out.
        const galaxy::render::RenderConfig config = galaxy::render::get_render_config();
        const bool mirror = galaxy::render::rt_part(config, galaxy::render::RtPart::Reflect) >= 3u &&
                            config.rt_quality != galaxy::render::kRtPresetReference &&
                            !::galaxy::diagnosing(::galaxy::kDiagnoseRtReference) && !::galaxy::diagnosing(::galaxy::kDiagnoseRtAdded) &&
                            !::galaxy::diagnosing(::galaxy::kDiagnoseRtMotion);
        return NEBULA_RAY_TRACING_ON | (stage != nullptr && stage->shadow_sets_kept ? 0u : NEBULA_RAY_TRACING_SHADOW_SETS) |
               (mirror ? NEBULA_RAY_TRACING_MIRROR : 0u);
    }
#else
        return 0u;
#endif
    case NEBULA_OPTION_SCENE_TEXTURES:
#if defined(NEBULA_HOST_RENDERER)
        return scene_textures_wait() ? 1u : 0u;
#else
        return 0u;
#endif
    default:
        return 0;
    }
}

static_assert(int{galaxy::saves::kDone} == NEBULA_SAVE_DONE && int{galaxy::saves::kNoFile} == NEBULA_SAVE_NO_FILE &&
                  int{galaxy::saves::kTooLarge} == NEBULA_SAVE_TOO_LARGE &&
                  int{galaxy::saves::kFailed} == NEBULA_SAVE_FAILED,
              "save results are the game's");

// The save half of the Mods page's REMOVED MODS line (services/save.md 5, services/mods.md 8): the mods' global files,
// luma/<id>.bin in the save folder. Any thread: a removed mod runs nothing, so its file is only read or deleted here.
fs::path mod_files_folder() {
    fs::path folder;
    return save_folder_path(gSaveDataPath.c_str(), &folder) ? folder / fs::path(galaxy::saves::kModFolder) : fs::path();
}

void kept_save_ids(std::vector<std::string>& out) {
    const fs::path folder = mod_files_folder();
    std::error_code error;
    if (folder.empty() || !fs::is_directory(folder, error)) {
        return;
    }
    for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        std::error_code status;
        if (it->is_regular_file(status) && it->path().extension() == ".bin") {
            out.push_back(galaxy::path_text(it->path().stem()));
        }
    }
}

fs::path kept_save_file(std::string_view id) {
    const fs::path folder = mod_files_folder();
    const std::string name = std::string(id) + ".bin";
    return folder.empty() || !galaxy::saves::is_plain_name(name) ? fs::path() : folder / fs::path(name);
}

std::uint64_t kept_save_bytes(std::string_view id) {
    const fs::path file = kept_save_file(id);
    std::error_code error;
    const std::uintmax_t size = file.empty() ? 0u : fs::file_size(file, error);
    return error ? 0u : static_cast<std::uint64_t>(size);
}

void forget_save(std::string_view id) {
    const fs::path file = kept_save_file(id);
    std::error_code error;
    if (!file.empty()) {
        fs::remove(file, error);
    }
}

// A save file operation on the save worker (the game polls the status): galaxy/save_files.h in the game's save folder
// (gSaveDataPath under the save folder, or a mod's save folder).
std::atomic<int> gSavesPending{0};
// The facts file of the mods' store attached this run (cook_mod_files), read into the mod log at the mods' start.
fs::path gCookFacts;
// Each planned file's store path and its mod: the cook's facts name the mod by it (mod-log.md 9).
std::vector<std::pair<std::string, std::string>> gCookOwners;
// The exit waits here for the saves still on the worker.
std::mutex gSavesMutex;
std::condition_variable gSavesDone;

// gFs stays one worker taking its queue in order (FIFO): the game's save, save.global's luma/<id>.bin writes and a
// broken file's luma/<id>.bin.broken copy rely on a later request never passing an earlier one.
void svc_save_request(void*, const NebulaSaveRequest* request) {
    gSaveOps.fetch_add(1, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
    if (galaxy::bench::running()) {  // a benchmark's blank save, in memory: the player's files are not opened
        *request->status = galaxy::bench::save_request(static_cast<int>(request->op), request->name != nullptr ? request->name : "",
                                                       request->buffer, request->size, request->length);
        return;
    }
    if (galaxy::calibration_scene::saves_in_memory()) {  // the HDR calibration's reference stage: as a benchmark's
        *request->status = galaxy::calibration_scene::save_request(
            static_cast<int>(request->op), request->name != nullptr ? request->name : "", request->buffer, request->size,
            request->length);
        return;
    }
#endif
    *request->status = NEBULA_SAVE_PENDING;
    gSavesPending.fetch_add(1, std::memory_order_acq_rel);
    const NebulaSaveRequest r = *request;
    const std::string name = r.name != nullptr ? r.name : "";
    auto operation = [r, name] {
        fs::path folder;
        galaxy::saves::Result result = galaxy::saves::kFailed;
        if (save_folder_path(name.starts_with("GameData.bin") ? gOldSaveDataPath.c_str() : gSaveDataPath.c_str(), &folder)) {
            // A mod's global file (services/save.md 7): "luma/<id>.bin" in the luma folder, made at its first write,
            // kept without backups; a file save.game's offer replaced (services/save.md 8): "luma/replaced/<name>",
            // written only. Any other name is the game's own file, a plain name in the save folder.
            std::string_view file = name;
            galaxy::saves::Backups backups = galaxy::saves::Backups::kKeep;
            bool placed = true;
            if (const std::string_view replaced = galaxy::saves::replaced_file_name(name); !replaced.empty()) {
                file = replaced;
                folder /= fs::path(galaxy::saves::kModFolder) / fs::path(galaxy::saves::kReplacedFolder);
                backups = galaxy::saves::Backups::kNone;
                std::error_code made;
                placed = r.op == NEBULA_SAVE_WRITE && (fs::create_directories(folder, made) || !made);
            } else if (name.starts_with(std::string(galaxy::saves::kModFolder) + "/")) {
                file = galaxy::saves::mod_file_name(name);
                folder /= fs::path(galaxy::saves::kModFolder);
                backups = galaxy::saves::Backups::kNone;
                std::error_code made;
                placed = !file.empty() && (r.op != NEBULA_SAVE_WRITE || fs::create_directories(folder, made) || !made);
            }
            if (placed) {
                switch (r.op) {
                case NEBULA_SAVE_READ:
                    result = galaxy::saves::read(folder, file, r.buffer, r.size, r.length);
                    break;
                case NEBULA_SAVE_WRITE:
                    result = galaxy::saves::write(folder, file, r.buffer, r.size, backups);
                    break;
                case NEBULA_SAVE_DELETE:
                    result = galaxy::saves::remove(folder, file);
                    break;
                }
            }
        }
#if defined(__cpp_lib_atomic_ref)
        std::atomic_ref<int32_t>(*r.status).store(result, std::memory_order_release);
#else  // libc++ without atomic_ref (Android NDK r28): the compiler's builtin, the same release store.
        __atomic_store_n(r.status, result, __ATOMIC_RELEASE);
#endif
        if (gSavesPending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            { const std::lock_guard<std::mutex> lock(gSavesMutex); }
            gSavesDone.notify_all();
        }
    };
    // Deterministic test mode: done before the call returns, as disc reads are, so the game's poll sees the same
    // status on the same frame every run.
    if (deterministic_mode()) {
        operation();
    } else {
        gFs.post(operation);
    }
}

// The settings the game asks the system for. The pointer mapping (native_input's virtual sensor bar) assumes the bar above
// the screen.
uint32_t svc_system_setting(void*, uint32_t item) {
    switch (item) {
    case NEBULA_SETTING_LANGUAGE:
#if defined(NEBULA_HOST_RENDERER)
        return static_cast<uint32_t>(galaxy::game_language_for_boot());  // the language setting
#else
        return 1;  // English
#endif
    case NEBULA_SETTING_SOUND_MODE:
#if defined(NEBULA_HOST_RENDERER)
        return static_cast<uint32_t>(galaxy::get_runtime_settings().audio.sound_output);
#else
        return 1;  // stereo
#endif
    case NEBULA_SETTING_WIDESCREEN:
        return 1;
    default:
        return 0;
    }
}

// Player 1: Nebula's controls (the bindings, keyboard and mouse, controllers; a real Wii Remote while it is in use, its
// sensors translated by wiimote::Motion), as a controller with a move stick, connected from the start. The game's read
// takes the sample. Headless, the controller lies still.
NebulaInputSample rest_input_sample() {
    NebulaInputSample sample{};
    sample.roll[0] = 1.0f;
    sample.distance = (0.2f / 0.383864f) / 0.3125f;
    sample.tilt[2] = 1.0f;
    sample.sub_tilt[2] = 1.0f;
    return sample;
}

#if defined(NEBULA_HOST_RENDERER)
static_assert(NEBULA_BUTTON_A == galaxy::input::kButtonA && NEBULA_BUTTON_B == galaxy::input::kButtonB &&
              NEBULA_BUTTON_1 == galaxy::input::kButton1 && NEBULA_BUTTON_2 == galaxy::input::kButton2 &&
              NEBULA_BUTTON_PLUS == galaxy::input::kButtonPlus && NEBULA_BUTTON_MINUS == galaxy::input::kButtonMinus &&
              NEBULA_BUTTON_HOME == galaxy::input::kButtonHome && NEBULA_BUTTON_UP == galaxy::input::kButtonUp &&
              NEBULA_BUTTON_DOWN == galaxy::input::kButtonDown && NEBULA_BUTTON_LEFT == galaxy::input::kButtonLeft &&
              NEBULA_BUTTON_RIGHT == galaxy::input::kButtonRight && NEBULA_BUTTON_C == galaxy::input::kButtonC &&
              NEBULA_BUTTON_Z == galaxy::input::kButtonZ,
              "the host's buttons are the game's");

NebulaInputSample input_sample(const galaxy::input::PlayerInput& input) {
    NebulaInputSample sample{};
    sample.buttons = galaxy::input::button_bits(input.buttons);
    sample.stick[0] = input.stick_x;
    sample.stick[1] = input.stick_y;
    sample.pointer_on = input.pointer_on ? 1u : 0u;
    sample.pointer[0] = input.pointer_x;
    sample.pointer[1] = input.pointer_y;
    sample.roll[0] = input.roll_x;
    sample.roll[1] = input.roll_y;
    sample.distance = input.distance;
    for (int i = 0; i < 3; ++i) {
        sample.tilt[i] = input.gravity[i];
        sample.sub_tilt[i] = input.nunchuk_gravity[i];
    }
    return sample;
}

// A device's tilt eased, and its spin held as presses: what a player does, one frame at a time.
class ControlsMotion {
public:
    void reset() {
        tilt_ = sub_tilt_ = {0.0f, 0.0f, 1.0f};
    }
    void apply(const galaxy::input::PlayerInput& input, NebulaInputSample& sample) {
        constexpr float kFollow = 0.15f;  // of the difference, each frame
        for (int i = 0; i < 3; ++i) {
            tilt_[i] += kFollow * (input.gravity[i] - tilt_[i]);
            sub_tilt_[i] += kFollow * (input.nunchuk_gravity[i] - sub_tilt_[i]);
            sample.tilt[i] = tilt_[i];
            sample.sub_tilt[i] = sub_tilt_[i];
        }
        sample.spin_presses = input.shake && !held_ ? 1u : 0u;
        sample.sub_spin_presses = input.nunchuk_shake && !sub_held_ ? 1u : 0u;
        held_ = input.shake;
        sub_held_ = input.nunchuk_shake;
    }

private:
    std::array<float, 3> tilt_{0.0f, 0.0f, 1.0f};
    std::array<float, 3> sub_tilt_{0.0f, 0.0f, 1.0f};
    bool held_ = false;
    bool sub_held_ = false;
};
ControlsMotion gControlsMotion;
galaxy::input::wiimote::Motion gRemoteMotion;
// One sample is taken at a time: on the game's thread when it reads, and on the input thread while a real remote is in use.
std::mutex gInputSampleLock;
std::atomic<bool> gRemoteConnected{false};  // a real remote is connected
std::atomic<bool> gInputRead{false};  // the game has read its input
std::atomic<bool> gControlsInput{true};  // player 1 is the controls (not a real remote)
std::atomic<uint8_t> gInputNunchuk{1};
std::atomic<uint8_t> gInputBattery{4};
std::atomic<uint8_t> gInputConnected{1};

// The chosen input mode has nothing to play with: the game shows its message (GameSystemErrorWatcher) until that changes.
// Looked at ten times a second.
bool input_device_missing(bool wii_remote) {
    static unsigned tick = 0;
    static bool missing = false;
    if (++tick % 10u != 0u) {
        return missing;
    }
    std::array<galaxy::input::ControllerInfo, galaxy::input::kMaxControllers> connected{};
    const bool controller = galaxy::host::connected_controllers(connected) != 0u;
#if defined(__ANDROID__)
    // Its screen is always in hand.
    const bool own_controls = galaxy::get_runtime_input_mode_state().mode == galaxy::RuntimeInputMode::Auto;
#else
    const bool own_controls = false;
#endif
    missing = !own_controls && galaxy::input_missing(galaxy::get_runtime_input_mode_state().mode, controller, wii_remote,
                                                     galaxy::host::keyboard_or_mouse_attached());
    return missing;
}

// The controls now (under gInputSampleLock). Each read renews the motor's state (a stale one stops): the game's, or a
// mod's when stronger (input.rumble).
galaxy::input::PlayerInput controls_input() {
    const int32_t mode = gStarPointerMode.load(std::memory_order_relaxed);
    const uint8_t strength = std::max(gMotor.load(std::memory_order_relaxed), galaxy::luma_input::mod_rumble(0));
    galaxy::host::native_game_rumble(strength, mode);
    galaxy::extensions::motor(strength);
    return galaxy::host::native_game_input(gGameFrames.load(std::memory_order_relaxed), mode,
                                           static_cast<uint8_t>(gTiltSection.load(std::memory_order_relaxed)));
}

// Whether the sample is someone playing: a button down, a stick pushed past its dead zone, a spin, or the pointer moved. Taken
// on the game's thread at each read, under gInputSampleLock; the time goes to the power policy (power_policy.h).
void note_input_activity(const NebulaInputSample& sample) {
    static float last_pointer[2] = {0.0f, 0.0f};
    constexpr float kStickPush = 0.25f;
    constexpr float kPointerMove = 0.01f;
    const bool pointer_moved = sample.pointer_on != 0u && (std::fabs(sample.pointer[0] - last_pointer[0]) > kPointerMove ||
                                                          std::fabs(sample.pointer[1] - last_pointer[1]) > kPointerMove);
    last_pointer[0] = sample.pointer[0];
    last_pointer[1] = sample.pointer[1];
    if (sample.buttons != 0u || std::fabs(sample.stick[0]) > kStickPush || std::fabs(sample.stick[1]) > kStickPush ||
        sample.spin_presses != 0u || sample.sub_spin_presses != 0u || pointer_moved) {
        gLastInputMs.store(steady_ms(), std::memory_order_relaxed);
    }
}
#endif

#if defined(NEBULA_HOST_RENDERER)
// The kind of device player 1 plays with now (LUMA_INPUT_DEVICE_*), for the input service: a real Wii Remote's motion
// (the remote branch of svc_input_read), a phone's on-screen controls, a controller, or the keyboard and mouse.
uint32_t main_device_kind(bool remote) {
    if (remote) {
        return LUMA_INPUT_DEVICE_MOTION;
    }
    if ((galaxy::extensions::flags() & NEBULA_EXT_CONTROLS_PAD) != 0u) {
        return LUMA_INPUT_DEVICE_PAD;  // a test extension's controller (its motor the extension's)
    }
    const std::optional<galaxy::input::DeviceKey> last = galaxy::host::last_input_device();
    if (!last || last->scheme == galaxy::input::ControlScheme::Keyboard) {
        return galaxy::host::touch_controls_present() ? LUMA_INPUT_DEVICE_TOUCH : LUMA_INPUT_DEVICE_KEYBOARD_MOUSE;
    }
    return LUMA_INPUT_DEVICE_PAD;
}

// Whether a mod's rumble reaches player 1's device: a controller whose motor the host drives (the controller worker on
// Windows, SDL's gamepads elsewhere; a test extension's, NEBULA_EXT_CONTROLS_PAD), or a real Wii Remote (its motor bit).
// None for the keyboard or a phone's screen.
bool main_motor(uint32_t device) {
    if (device == LUMA_INPUT_DEVICE_PAD && (galaxy::extensions::flags() & NEBULA_EXT_CONTROLS_PAD) != 0u) {
        return true;
    }
#if defined(NEBULA_REAL_WIIMOTE)
    if (device == LUMA_INPUT_DEVICE_MOTION) {
        return true;
    }
#endif
#if defined(_WIN32)
    return device == LUMA_INPUT_DEVICE_PAD && !galaxy::host::rumble_output_disabled();
#else
    return device == LUMA_INPUT_DEVICE_PAD;
#endif
}
#endif

// The players svc_input_read serves: player 1's live controls, and with the renderer player 2 (co-star mode).
enum HostPlayer : uint32_t {
    kHostPlayerMain,
#if defined(NEBULA_HOST_RENDERER)
    kHostPlayerCoStar,
#endif
    kHostPlayers
};

uint32_t svc_input_players(void*) {
    return kHostPlayers;
}

uint32_t svc_input_read(void*, uint32_t player, NebulaInputDevice* device, NebulaInputSample* sample) {
#if defined(NEBULA_HOST_RENDERER)
    // A benchmark, and the HDR calibration's reference stage under the menu, play nothing: player 1's remote connected
    // and at rest.
    if (galaxy::bench::running() || galaxy::calibration_scene::live()) {
        if (device != nullptr) {
            *device = {};
            device->connected = player == kHostPlayerMain ? 1 : 0;
            device->nunchuk = device->connected;
            device->battery = device->connected != 0 ? 4 : 0;
        }
        if (player != kHostPlayerMain || sample == nullptr) return 0;
        // The controls are still read, for what reaches the host through them (a controller's B stops the run), and
        // the game is given none of it.
        if (gControlsInput.load(std::memory_order_acquire)) {
            const std::lock_guard<std::mutex> lock(gInputSampleLock);
            (void)controls_input();
        }
        *sample = rest_input_sample();
        gInputRead.store(true, std::memory_order_release);
        gInputRead.notify_one();
        return 1;
    }
#endif
    if (device != nullptr) {
        *device = {};
#if defined(NEBULA_HOST_RENDERER)
        // Player 2, when someone plays it (co-star mode): the pointer and its buttons, no move stick.
        if (player == kHostPlayerCoStar) {
            galaxy::input::PlayerInput co_star{};
            if (galaxy::host::native_game_co_star(&co_star)) {
                device->connected = 1;
                device->battery = 4;
                if (sample != nullptr) {
                    *sample = input_sample(co_star);
                    // Player 2 is always a controller (the last one attached); the worker drives no motor for it.
                    galaxy::luma_input::game_read(1u, sample, LUMA_INPUT_DEVICE_PAD, false);
                    return 1;
                }
            }
            return 0;
        }
#endif
        if (player == kHostPlayerMain) {
            device->connected = 1;
#if defined(NEBULA_HOST_RENDERER)
            device->connected = gInputConnected.load(std::memory_order_relaxed);
            device->nunchuk = gInputNunchuk.load(std::memory_order_relaxed);
            device->battery = gInputBattery.load(std::memory_order_relaxed);
#else
            device->nunchuk = 1;
            device->battery = 4;
#endif
        }
    }
    if (player != 0 || sample == nullptr) {
        return 0;
    }
#if defined(NEBULA_HOST_RENDERER)
    gInputRead.store(true, std::memory_order_release);
    gInputRead.notify_one();
    if (gControlsInput.load(std::memory_order_acquire)) {
        const std::lock_guard<std::mutex> lock(gInputSampleLock);
        const galaxy::input::PlayerInput input = controls_input();
        *sample = input_sample(input);
        gControlsMotion.apply(input, *sample);
        const bool missing = input_device_missing(gRemoteConnected.load(std::memory_order_relaxed));
        galaxy::host::set_input_missing_shown(missing);
        gInputConnected.store(missing ? 0u : 1u, std::memory_order_relaxed);
        const uint32_t device = missing ? LUMA_INPUT_DEVICE_NONE : main_device_kind(false);
        galaxy::luma_input::game_read(0u, sample, device, main_motor(device));
    } else {
        galaxy::input::wiimote::MotionFrame frame;
        const galaxy::input::PlayerInput input = gRemoteMotion.read(frame);
        *sample = input_sample(input);
        for (int i = 0; i < 3; ++i) {
            sample->tilt[i] = frame.tilt[i];
            sample->sub_tilt[i] = frame.sub_tilt[i];
        }
        sample->spin_presses = frame.spin_presses;
        sample->sub_spin_presses = frame.sub_spin_presses;
        galaxy::luma_input::game_read(0u, sample, LUMA_INPUT_DEVICE_MOTION, main_motor(LUMA_INPUT_DEVICE_MOTION));
#if defined(NEBULA_REAL_WIIMOTE)
        // Its motor is on or off: a strength is how often it is on (the game's, or a mod's when stronger).
        static uint32_t motor_share = 0;
        motor_share += std::max(gMotor.load(std::memory_order_relaxed), galaxy::luma_input::mod_rumble(0));
        const bool motor_on = motor_share >= 255u;
        if (motor_on) {
            motor_share -= 255u;
        }
        galaxy::host::real_wiimote_set_rumble(motor_on);
#endif
    }
    note_input_activity(*sample);
    return 1;
#else
    // The headless host samples no controls: a controller at rest, which a mod's injection (input.inject) replaces.
    *sample = rest_input_sample();
    galaxy::luma_input::game_read(0u, sample, LUMA_INPUT_DEVICE_NONE, false);
    return 1;
#endif
}

void svc_input_control(void*, uint32_t player, uint32_t what, uint32_t value) {
#if defined(NEBULA_HOST_RENDERER)
    if (player == 0 && what == NEBULA_INPUT_RESET) {
        const std::lock_guard<std::mutex> lock(gInputSampleLock);
        gControlsMotion.reset();
        gRemoteMotion.reset();
        return;
    }
#endif
    if (player != 0 || what != NEBULA_INPUT_RUMBLE) {
        return;  // player 1's controller is never let go; there are no others
    }
    gMotor.store(static_cast<uint8_t>(value), std::memory_order_relaxed);
#if defined(NEBULA_REAL_WIIMOTE)
    if (!galaxy::host::real_wiimote_in_use()) {
        galaxy::host::native_game_rumble(static_cast<uint8_t>(value), gStarPointerMode.load(std::memory_order_relaxed));
    }
#elif defined(NEBULA_HOST_RENDERER)
    galaxy::host::native_game_rumble(static_cast<uint8_t>(value), gStarPointerMode.load(std::memory_order_relaxed));
#endif
}

#if defined(NEBULA_HOST_RENDERER)
void input_thread() {
    if (deterministic_mode()) {
        return;  // the read takes the sample (svc_input_read)
    }
#if !defined(NEBULA_REAL_WIIMOTE)
    return;
#else
    name_thread(L"Nebula Host Input");
    gInputRead.wait(false, std::memory_order_acquire);
    const auto period = std::chrono::milliseconds(10);
    auto next = Clock::now() + period;
    galaxy::host::ensure_real_wiimote_started();
    // The remote's data reports, IR camera on: with its Nunchuk, buttons, accelerometer, IR and extension; without.
    galaxy::host::real_wiimote_set_report(0x37);
    galaxy::host::real_wiimote_set_ir(true);
    // A real Wii Remote: `remote` while one is connected (its calibration), `real` while the game plays with it.
    std::optional<galaxy::host::RealWiimoteConnection> remote;
    galaxy::input::wiimote::Reader reader;
    galaxy::input::wiimote::MotionPointer motion_pointer;
    bool real = false;
    std::optional<galaxy::input::DeviceKey> device_when_real;
    const auto use_remote = [&](bool on) {
        if (on == real || (on && !remote.has_value())) return;
        real = on;
        if (on) {
            galaxy::host::set_input_missing_shown(false);
            gInputConnected.store(1u, std::memory_order_relaxed);
        }
        galaxy::host::real_wiimote_set_active(on);
#if defined(NEBULA_HOST_SDL)
        galaxy::host::real_wiimote_taken(on);
#endif
        gControlsInput.store(!on, std::memory_order_release);
        gInputNunchuk.store(on ? (remote->nunchuk ? 1u : 0u) : 1u, std::memory_order_relaxed);
        gInputBattery.store(on ? remote->battery : 4u, std::memory_order_relaxed);
        device_when_real = galaxy::host::last_input_device();
        next = Clock::now() + period;
        log_line("[wiimote]", on ? "in use" : "keyboard, mouse and controllers in use");
    };
    for (;;) {
        // A real Wii Remote's reports become samples as they arrive: in the Wii Remote input mode while it is
        // connected, in Auto from its first button press until a key, a mouse button or a controller is used. The
        // keyboard, mouse and controllers are read every 10 ms meanwhile (for that, and the settings menu).
        std::array<uint8_t, 22> real_report{};
        std::size_t real_size = 0;
        galaxy::host::RealWiimoteConnection real_connection{};
        const auto until_tick =
            std::chrono::duration_cast<std::chrono::milliseconds>(next - Clock::now()).count();
        // Not playing with it: nothing to do until a report or a connection arrives.
        const galaxy::host::RealWiimoteEvent event = galaxy::host::real_wiimote_wait(
            real ? static_cast<uint32_t>(std::clamp<long long>(until_tick, 0, 10)) : 3600000u, real_report, real_size,
            real_connection);
        const bool wii_remote_mode = galaxy::is_wii_remote_mode(galaxy::get_runtime_input_mode_state().mode);
        if (event == galaxy::host::RealWiimoteEvent::Connected) {
            remote = real_connection;
            gRemoteConnected.store(true, std::memory_order_relaxed);
            reader.connect(real_connection.eeprom, real_connection.extension_calibration, real_connection.nunchuk,
                           real_connection.motion_plus
                               ? std::span<const uint8_t>(real_connection.motion_plus_calibration)
                               : std::span<const uint8_t>());
            motion_pointer.reset();
            galaxy::host::real_wiimote_set_report(real_connection.nunchuk ? 0x37 : 0x33);
            if (real) {
                gInputNunchuk.store(real_connection.nunchuk ? 1u : 0u, std::memory_order_relaxed);
                gInputBattery.store(real_connection.battery, std::memory_order_relaxed);
            } else if (wii_remote_mode) {
                use_remote(true);
            }
            continue;
        }
        if (event == galaxy::host::RealWiimoteEvent::Disconnected) {
            use_remote(false);
            remote.reset();
            gRemoteConnected.store(false, std::memory_order_relaxed);
            continue;
        }
        if (event == galaxy::host::RealWiimoteEvent::Battery) {
            if (remote.has_value()) remote->battery = real_connection.battery;
            if (real) gInputBattery.store(real_connection.battery, std::memory_order_relaxed);
            continue;
        }
        if (event == galaxy::host::RealWiimoteEvent::Report) {
            const std::span<const uint8_t> bytes(real_report.data(), real_size);
            if (!real && (wii_remote_mode || galaxy::host::real_wiimote_buttons_held(bytes))) use_remote(true);
            galaxy::input::PlayerInput input{};
            galaxy::input::wiimote::MotionRates rates;
            if (real && reader.read(bytes, input, &rates)) {
                // The MotionPlus on (the motion pointer mode): the pointer is its, 2 recenters it.
                if (remote.has_value() && remote->motion_plus) {
                    const auto now_us = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
                    motion_pointer.apply(input, rates, now_us);
                }
                gRemoteMotion.add(input);
            }
            continue;
        }
        if (!real || Clock::now() < next) {
            continue;
        }
        next += period;
        const std::lock_guard<std::mutex> lock(gInputSampleLock);
        (void)controls_input();  // for the device last used, and the settings menu
        // Another device used (in Auto): back to the keyboard, mouse and controllers, with this sample.
        if (!galaxy::is_wii_remote_mode(galaxy::get_runtime_input_mode_state().mode) &&
            galaxy::host::last_input_device() != device_when_real) {
            use_remote(false);
        }
    }
#endif
}
#endif

#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/wiimote_speaker.h"
#endif

// The remote speaker's sound, player 1's (6 kHz blocks): the audio output mixes it at the speaker gain.
void svc_speaker_samples(void*, uint32_t player, const int16_t* samples, uint32_t count) {
#if defined(NEBULA_HOST_RENDERER)
    if (player == 0u && samples != nullptr) {
        galaxy::input::speaker::game_speaker().push(std::span<const std::int16_t>(samples, count), 6000u);
    }
#else
    (void)player, (void)samples, (void)count;
#endif
}

// The game's sound (native_audio.h), on its audio thread. Headless, nothing is mixed.
void svc_audio_setup(void*, NebulaVoice* voices, uint32_t count, const NebulaEffectLine* effects) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->setup(voices, count, effects, !deterministic_mode());
#else
    (void)voices, (void)count, (void)effects;
#endif
}

void svc_audio_mix(void*, float gain, uint32_t used) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->mix(gain, used);
#else
    (void)gain;
    (void)used;
#endif
    gSoundSubframes.fetch_add(1, std::memory_order_relaxed);
}

void svc_audio_frame(void*) {
    if (::galaxy::diagnosing(::galaxy::kDiagnoseFileReads)) {
        std::fprintf(stderr, "[read] sound frame %llu at frame %llu\n", static_cast<unsigned long long>(gSoundBlocks.load(std::memory_order_relaxed)),
                     static_cast<unsigned long long>(gDeterministicFrames.load(std::memory_order_relaxed)));
    }
#if defined(NEBULA_HOST_RENDERER)
    gAudio->frame();
#endif
}

// The movie's sound: headless, every frame is taken and dropped.
int svc_movie_audio(void*, const void* record, uint32_t size) {
#if defined(NEBULA_HOST_RENDERER)
    gLastMovieMs.store(steady_ms(), std::memory_order_relaxed);  // a cutscene is playing (power_policy.h)
    return gAudio->movie_audio(record, size);
#else
    (void)record, (void)size;
    return 1;
#endif
}

double svc_movie_audio_position(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return gAudio->movie_audio_position();
#else
    return 0.0;
#endif
}

void svc_movie_audio_control(void*, uint32_t what, int32_t value, int32_t milliseconds) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->movie_audio_control(what, value, milliseconds);
#else
    (void)what, (void)value, (void)milliseconds;
#endif
}

void svc_audio_control(void*, uint32_t what, uint32_t value) {
    if (::galaxy::diagnosing(::galaxy::kDiagnoseFileReads)) {
        std::fprintf(stderr, "[read] sound clock %u at frame %llu\n", what, static_cast<unsigned long long>(gDeterministicFrames.load()));
    }
    {
        std::lock_guard<std::mutex> lock(gSoundClockLock);
        if (what == NEBULA_AUDIO_START) {
            gSoundClockRunning = true;
            gSoundTicksPerSecond = value;
        } else if (what == NEBULA_AUDIO_STOP) {
            gSoundClockRunning = false;
        }
        gSoundClockWake.notify_one();
    }
#if defined(NEBULA_HOST_RENDERER)
    if (what == NEBULA_AUDIO_START) {
        gAudio->start(audio_frame_tick);
    } else if (what == NEBULA_AUDIO_STOP) {
        gAudio->stop();
    }
#endif
}

#if defined(NEBULA_HOST_RENDERER)
// The benchmark in the running game (bench::Options::here), from the settings menu's Run benchmark or the home screen's
// Benchmark: the game goes back to its title for it (NEBULA_HOME_MENU_RESET) and again at its end, then the settings
// menu opens on its result.
std::filesystem::path gPakFolder;
bool start_benchmark_here() {
    galaxy::bench::Options options;
    options.pak_folder = gPakFolder;
    options.here = true;
    options.mods_loaded = !galaxy::mods::session_mods().active.empty();
    options.ended = [](const std::filesystem::path& result) {
        log_line("host:", "the benchmark ended: its results open at the title");
        {
            const std::lock_guard<std::mutex> lock(gResultsMutex);
            gResultsPending = result;
        }
        gResultsWaiting.store(true, std::memory_order_release);
    };
    if (!galaxy::bench::start(options)) return false;
    log_line("host:", "the benchmark runs in the game");
    return true;
}
#endif

#if defined(NEBULA_UPDATE_CHECK)
bool gUpdateOffered = false;  // the game thread's (svc_home_menu)
bool gUpdateNoticed = false;  // the title's notice was shown this session
#endif

int svc_home_menu(void*, uint32_t what, uint32_t value) {
#if defined(NEBULA_HOST_RENDERER)
    // Launcher mode's home screen is drawn as this menu while the HOME Menu itself is closed.
    const bool launcher = galaxy::ui::launcher_visible() && !galaxy::render::home_menu_visible();
    switch (what) {
    case NEBULA_HOME_MENU_LAUNCHER_OPEN: {
        const bool opened = galaxy::ui::launcher_open();
#if defined(NEBULA_UPDATE_CHECK)
        if (opened) {
            if (const std::optional<galaxy::update::Release> release = galaxy::update::offer(galaxy::update::kFreshBudgetMs)) {
                galaxy::ui::launcher_offer_update(release->version, release->notes, galaxy::update::can_install());
                gUpdateOffered = true;
            }
        }
#endif
        return opened ? 1 : 0;
    }
    case NEBULA_HOME_MENU_LAUNCHER_NOTICE: {
#if defined(NEBULA_UPDATE_CHECK)
        // The check started with the game (Check for updates at start): its answer, once a session, never waited for.
        if (gUpdateNoticed || galaxy::bench::running()) return 0;
        const std::optional<galaxy::update::Release> release = galaxy::update::poll();
        if (!release) return 0;
        gUpdateNoticed = true;
        galaxy::ui::launcher_offer_update(release->version, release->notes, galaxy::update::can_install());
        gUpdateOffered = true;
        return galaxy::ui::launcher_open_notice() ? 1 : 0;
#else
        return 0;
#endif
    }
    case NEBULA_HOME_MENU_LAUNCHER_POLL:
#if defined(NEBULA_UPDATE_CHECK)
        // An answer that came after the screen opened (never waits).
        if (!gUpdateOffered && galaxy::ui::launcher_visible()) {
            if (const std::optional<galaxy::update::Release> release = galaxy::update::poll()) {
                galaxy::ui::launcher_offer_update(release->version, release->notes, galaxy::update::can_install());
                gUpdateOffered = true;
            }
        }
#if !defined(_WIN32)
        // The notice's Update on Linux and macOS: the update downloads and installs behind the game, which goes on as
        // for Play; the System page shows its progress and restarts into it. (On Windows Nebula Setup installs it once
        // the game has left: svc_app_exit.)
        if (galaxy::ui::launcher_poll() == galaxy::ui::LauncherModel::kChoiceUpdate) {
            if (const std::optional<galaxy::update::Release> release = galaxy::update::poll()) {
                if (!galaxy::update::start_install(*release)) log_line("host:", "the update could not start");
            }
            return galaxy::ui::LauncherModel::kChoicePlay;
        }
#endif
#endif
        // The home screen's Benchmark: the game goes on as for Play and the run takes it back to its title.
        if (galaxy::ui::launcher_poll() == galaxy::ui::LauncherModel::kChoiceBenchmark) {
            if (!start_benchmark_here()) log_line("host:", "the benchmark could not start");
            return galaxy::ui::LauncherModel::kChoicePlay;
        }
        return galaxy::ui::launcher_poll();
    case NEBULA_HOME_MENU_LAUNCHER_TEXT:
        return static_cast<int>(galaxy::ui::launcher_text(value >> 16, value & 0xFFFFu));
    case NEBULA_HOME_MENU_RESET:
        return galaxy::bench::take_reset() || galaxy::calibration_scene::take_reset() ? 1 : 0;
    case NEBULA_HOME_MENU_RESET_DONE:
        galaxy::bench::reset_done();
        galaxy::calibration_scene::reset_done();
        return 0;
    default:
        break;
    }
    if (launcher && (what == NEBULA_HOME_MENU_VIEW || what == NEBULA_HOME_MENU_POINT || what == NEBULA_HOME_MENU_CHOOSE)) {
        if (what == NEBULA_HOME_MENU_POINT) {
            galaxy::ui::launcher_point(value);
        } else if (what == NEBULA_HOME_MENU_CHOOSE) {
            galaxy::ui::launcher_choose(value);
        }
        return what == NEBULA_HOME_MENU_VIEW ? galaxy::ui::launcher_view() : 0;
    }
    switch (what) {
    case NEBULA_HOME_MENU_OPEN:
        galaxy::render::home_menu_open();
        return 0;
    case NEBULA_HOME_MENU_POLL:
        return galaxy::render::home_menu_poll();
    case NEBULA_HOME_MENU_CLOSE:
        galaxy::render::home_menu_close();
        return 0;
    case NEBULA_HOME_MENU_VIEW:
        return galaxy::render::home_menu_view();
    case NEBULA_HOME_MENU_POINT:
        galaxy::render::home_menu_point(value);
        return 0;
    case NEBULA_HOME_MENU_CHOOSE:
        galaxy::render::home_menu_choose(value);
        return 0;
    default:
        return 0;
    }
#else
    (void)value;
    return what == NEBULA_HOME_MENU_VIEW ? -1 : 0;  // headless: the menu closes at once
#endif
}

// Set when an extension asks the game to end (extension_quit): the main loop closes the game.
std::atomic<bool> gTourDone{false};
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
HANDLE gHostWake = nullptr;
#endif

// What the host gives its extensions (nebula_host_extension.h).
void extension_log(void*, const char* line) {
    log_line("extension:", line);
}
void extension_quit(void*) {
    gTourDone.store(true, std::memory_order_relaxed);
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
    if (gHostWake != nullptr) {
        SetEvent(gHostWake);
    }
#endif
}
#if defined(NEBULA_HOST_RENDERER)
void extension_menu_key(void*, uint32_t key) {
    // An open text request takes the editing keys first, as the window's keyboard gives them to it.
    if (galaxy::luma_input::text_open()) {
        using galaxy::luma_input::TextKey;
        switch (key) {
        case NEBULA_EXT_KEY_ENTER: galaxy::luma_input::text_key(TextKey::Enter); return;
        case NEBULA_EXT_KEY_ESCAPE: galaxy::luma_input::text_key(TextKey::Escape); return;
        case NEBULA_EXT_KEY_BACKSPACE: galaxy::luma_input::text_key(TextKey::Backspace); return;
        case NEBULA_EXT_KEY_LEFT: galaxy::luma_input::text_key(TextKey::Left); return;
        case NEBULA_EXT_KEY_RIGHT: galaxy::luma_input::text_key(TextKey::Right); return;
        default: break;
        }
    }
    static constexpr int kKeys[] = {0,         VK_UP,  VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN,
                                    VK_ESCAPE, VK_TAB, VK_BACK, VK_SPACE, VK_F1};
    if (key != 0u && key < std::size(kKeys)) {
        galaxy::render::native_settings_post_keyboard_key(static_cast<std::uintptr_t>(kKeys[key]));
    }
}
uint32_t extension_menu_open(void*) {
    if (galaxy::render::home_menu_visible()) {
        return NEBULA_EXT_MENU_HOME;
    }
    return galaxy::render::native_settings_overlay_visible() ? NEBULA_EXT_MENU_SETTINGS : NEBULA_EXT_MENU_NONE;
}
void extension_open_settings(void*, uint32_t page) {
    galaxy::render::host_window::open_native_settings(page);
}
void extension_resize_window(void*, uint32_t width, uint32_t height) {
    galaxy::render::host_window::resize_window_client(width, height);
}
// The harness's text step: the code points SDL's text input hands its host (the Win32 window's WM_CHAR feeds each of
// them to the same text_char).
void extension_type_text(void*, const char* utf8) {
    galaxy::luma_input::text_utf8(utf8);
}
#endif

void extension_take_out_mod(void*, const char* mod_id) {
    if (mod_id != nullptr) {
        galaxy::mods::take_out(mod_id);
    }
}

void extension_reload_mod(void*, const char* mod_id) {
    if (mod_id != nullptr) {
        galaxy::mods::reload(mod_id);
    }
}

// The Mods page's FORGET (native_settings.cpp) for an id its REMOVED MODS line lists, and only then.
void extension_forget_mod(void*, const char* mod_id) {
    if (mod_id == nullptr) {
        return;
    }
    std::vector<galaxy::luma_mods::KeptMod> kept;
    galaxy::luma_mods::kept(kept);
    const bool listed = std::any_of(kept.begin(), kept.end(),
                                    [mod_id](const galaxy::luma_mods::KeptMod& row) { return row.id == mod_id; });
    if (!listed) {
        log_line("mods:", (std::string(mod_id) + " is not on the REMOVED MODS line: nothing to forget").c_str());
        return;
    }
    galaxy::luma_mods::forget(mod_id);
}

void attach_extensions() {
    const std::vector<fs::path>& files = ::galaxy::launch_args().extensions;
    if (files.empty()) {
        return;
    }
    static NebulaHostForExtension host{};
    host.version = NEBULA_HOST_EXTENSION_VERSION;
    host.size = sizeof(host);
    host.log = extension_log;
    host.quit = extension_quit;
    host.take_out_mod = extension_take_out_mod;
    host.reload_mod = extension_reload_mod;
    host.forget_mod = extension_forget_mod;
    host.started_again = ::galaxy::launch_args().started_again;
#if defined(NEBULA_HOST_RENDERER)
    host.menu_key = extension_menu_key;
    host.menu_open = extension_menu_open;
    host.open_settings = extension_open_settings;
    host.resize_window = extension_resize_window;
    host.type_text = extension_type_text;
#endif
    std::string error;
    if (!galaxy::extensions::attach(files, host, error)) {
        die("--extension %s", error.c_str());
    }
    log_line("host:", (std::to_string(files.size()) + " extension(s) attached").c_str());
}

// The settings' galaxy select (its ADVANCED page): a travel the player asked for, through the game's own stage change,
// and the Grand Stars with the Observatory they power.
int svc_galaxy_travel(void*, char* stage, uint32_t capacity, int32_t* star) {
#if defined(NEBULA_HOST_RENDERER)
    if (galaxy::bench::running()) return galaxy::bench::scene_frame(gSceneStage, stage, capacity, star);
    if (galaxy::calibration_scene::live()) return galaxy::calibration_scene::scene_frame(gSceneStage, stage, capacity, star);
    std::size_t index = 0u;
    std::uint32_t chosen = 0u;
    if (galaxy::render::take_galaxy_travel(index, chosen)) {
        std::snprintf(stage, capacity, "%s", galaxy::render::kGalaxyStages[index].id);
        *star = static_cast<int32_t>(chosen);
        return NEBULA_GALAXY_TRAVEL_STAGE;
    }
    if (galaxy::render::take_grand_stars()) {
        std::snprintf(stage, capacity, "%s", galaxy::render::kObservatoryId);
        *star = 6;
        return NEBULA_GALAXY_TRAVEL_GRAND_STARS;
    }
#else
    (void)stage;
    (void)capacity;
    (void)star;
#endif
    return NEBULA_GALAXY_TRAVEL_NONE;
}

// A restart starts this executable again with the same arguments (the game's boot); the
// other power events close the game.
// Every way out of the game: pending saves written (bounded: a dead drive does not hold the game), counters flushed.
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
// Up to kMaxGames games per data folder, so a player can host and join on one computer to test co-op. The first plays
// as always; each further one is game 2, 3, ... with its own folder for the files it writes (galaxy/game_instance.h),
// so two never write the same saves or settings. A start past the last brings the first's window forward and ends.
// Test runs have their own folders, so they run side by side.
constexpr unsigned kMaxGames = 4u;
// The game a restart (finish_and_exit) starts again as, so a second game stays the second.
constexpr wchar_t kGameInstanceVariable[] = L"NEBULA_GAME_INSTANCE";
HANDLE gOneGame = nullptr;

// A second game's folder starts with copies of the first's settings, graphics settings and mods list, the window
// windowed (two borderless games on one screen would cover each other). A file it already has stays as it is.
void seed_game_instance(const fs::path& folder, const fs::path& settings, const fs::path& config) {
    std::error_code error;
    fs::create_directories(folder, error);
    const auto copy_once = [&](const fs::path& from, const fs::path& to) {
        std::error_code copy_error;
        if (from.empty() || fs::exists(to, copy_error) || !fs::is_regular_file(from, copy_error)) return false;
        return fs::copy_file(from, to, copy_error) && !copy_error;
    };
    if (!settings.empty()) {
        copy_once(settings / "runtime_settings.ini", folder / "runtime_settings.ini");
        copy_once(settings / "mods.ini", folder / "mods.ini");
    }
    if (copy_once(config, folder / "config.ini")) {
        std::string text;
        if (std::FILE* file = galaxy::open_path(folder / "config.ini", "rb")) {
            char buffer[4096];
            for (std::size_t read; (read = std::fread(buffer, 1, sizeof(buffer), file)) != 0u;) text.append(buffer, read);
            std::fclose(file);
        }
        std::string out;
        for (std::size_t at = 0; at < text.size();) {
            std::size_t end = text.find('\n', at);
            end = end == std::string::npos ? text.size() : end + 1u;
            const std::string_view line(text.data() + at, end - at);
            out += line.starts_with("display_mode=") ? "display_mode=" + std::to_string(galaxy::render::kDisplayWindowed) + "\n"
                                                     : std::string(line);
            at = end;
        }
        (void)galaxy::io::write_file_atomically(folder / "config.ini", out);
    }
}

// A game's window key, from its mutex's name (never 0).
std::uint64_t game_window_key(const std::wstring& name) {
    std::uint64_t key = 1469598103934665603ull;
    for (const wchar_t c : name) {
        key = (key ^ static_cast<std::uint64_t>(c)) * 1099511628211ull;
    }
    return key != 0u ? key : 1u;
}

// True when this start should end (every game slot taken).
bool claim_game_instance(int argc, char** argv) {
    // The mods tool (--import-mod, --list-mods, --remove-mod) plays no game: it works on the first's mods.
    if (argc >= 2 && (std::strcmp(argv[1], "--import-mod") == 0 || std::strcmp(argv[1], "--list-mods") == 0 ||
                      std::strcmp(argv[1], "--remove-mod") == 0)) {
        return false;
    }
    const fs::path settings = galaxy::runtime_settings_folder();
    std::wstring base = L"Local\\NebulaGame-";
    for (const wchar_t c : settings.wstring()) {
        base += c == L'\\' || c == L':' ? L'_' : c;
    }
    // A restart asks for the game it was; otherwise the first free one.
    unsigned wanted = 0u;
    wchar_t value[8]{};
    if (GetEnvironmentVariableW(kGameInstanceVariable, value, 8) != 0u) {
        const unsigned digit = static_cast<unsigned>(value[0]) - static_cast<unsigned>(L'0');
        if (digit >= 1u && digit <= kMaxGames && value[1] == L'\0') wanted = digit;
        SetEnvironmentVariableW(kGameInstanceVariable, nullptr);
    }
    for (unsigned attempt = 0u; attempt <= kMaxGames; ++attempt) {
        const unsigned number = attempt == 0u ? wanted : attempt;
        if (number == 0u || (attempt != 0u && number == wanted)) continue;
        // The first game's name is the one every earlier Nebula used.
        const std::wstring name = number == 1u ? base : base + L"-" + std::to_wstring(number);
        HANDLE mutex = CreateMutexW(nullptr, TRUE, name.c_str());
        if (mutex == nullptr) return false;  // nothing to tell games apart with: play, as the first
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutex);
            continue;
        }
        gOneGame = mutex;
        // The game's key: its window carries it, and a start past the last finds the first's window by it.
        galaxy::render::host_window::set_instance_key(game_window_key(name));
        if (number > 1u && !settings.empty()) {
            const fs::path folder = settings / "instances" / std::to_wstring(number);
            seed_game_instance(folder, settings, galaxy::render::render_config_path());
            galaxy::set_game_instance(number, folder);
        }
        return false;
    }
    log_line("host:", "every game slot is taken");
    // Up to 3 s: the first start may be opening its window still.
    // Files it was started to import (Open with, --import) go to the running game, which imports them.
    if (!galaxy::render::host_window::show_existing_instance(game_window_key(base), 3000u, galaxy::launch_args().imports)) {
        log_line("host:", "the running game has no window to bring forward");
    }
    return true;
}

#endif

// A second game's saves (galaxy/game_instance.h): its own folder, which starts as a copy of the first's.
fs::path game_instance_saves(const fs::path& saves) {
    const fs::path& folder = galaxy::game_instance_folder();
    if (folder.empty()) return saves;
    const fs::path own = folder / "saves";
    std::error_code error;
    if (!fs::exists(own, error)) {
        fs::create_directories(own, error);
        if (fs::is_directory(saves, error)) {
            fs::copy(saves, own, fs::copy_options::recursive | fs::copy_options::skip_existing, error);
            if (error) std::fprintf(stderr, "[host] game %u's saves: copying the first's failed (%s)\n",
                                    galaxy::game_instance(), error.message().c_str());
        }
    }
    return own;
}

// Whether finish_and_exit(true) starts the game again on this host (Windows: a new process; Android: the activity's
// restartGame; Linux and macOS: an exec of the running executable). Where it does not (an iOS app), it only ends the
// game, so a caller that would start again goes on instead.
constexpr bool kHostStartsAgain =
#if defined(__APPLE__) && TARGET_OS_IPHONE
    false;
#else
    true;
#endif
#if defined(_WIN32)
// A process's creation time as one number (FILETIME), 0 when it cannot be read.
unsigned long long creation_of(HANDLE process) {
    FILETIME made{}, ended{}, kernel{}, user{};
    if (!GetProcessTimes(process, &made, &ended, &kernel, &user)) return 0;
    return (static_cast<unsigned long long>(made.dwHighDateTime) << 32u) | made.dwLowDateTime;
}

// The hand-over's event, named for the old process's id and creation time: only the process it started finds it.
std::wstring hand_over_event_name(unsigned long pid, unsigned long long created) {
    return L"Local\\NebulaHandOver-" + std::to_wstring(pid) + L"-" + std::to_wstring(created);
}

// The hand-over of a start-again, the old process's half (finish_and_exit, after the new process is started; saves,
// settings, records and the log are out by then). The new process needs this one's files, not its end: ending a
// process with a renderer takes the system seconds to a minute (its device, its memory), which a player would wait.
// So every other thread is stopped (as the end would stop them, only now), the files the new process's cook replaces
// are let go, the event says so, and the process ends.
// The cook and the store need these of the mods folder's .cooked (read from the code): mods.pak (the store's overlay:
// its file, its mapping, its view, and views given out by map_private), mods.plan, mods.signature, mods.facts,
// mods.left_out, built.shaders, mods.sources, validate, code-layers and its restart file. All but mods.pak are read or
// written whole and closed, so only mods.pak is held: the store lets go of its handles and every view of a file under
// .cooked is unmapped. The running copies of the mods' libraries (.cooked/code/<process>-<n>) stay: the new process
// makes its own. Elsewhere: game.pak (read-shared both ways), mods.ini and the settings (written whole, atomically),
// the shader cache (read whole at the start, replaced atomically), the Vulkan compile journal (its handle closed once
// mapped), the saves (waited for before the hand-over), the log and a test's audio file (opened sharing reads and
// writes): none is held so the new process cannot open it.
// The new process is started first and the files let go after: once the threads are stopped only system calls that
// take no lock of the process's run (one of them may hold the heap's lock or the C runtime's), and starting a process,
// naming a mapped file (GetMappedFileNameW) or writing the log are not those. So the .cooked views are found and every
// mapped view counted before the stop; after it they are unmapped, and so is any view mapped in between (unnamed: the
// process ends next). A backstop thread, left running, sets the event and ends the process after 5 s, should a call
// here still wait on a stopped thread: a process left behind would hold the files and its window.
constexpr DWORD kHandOverBackstopMs = 5000;
constexpr size_t kHandOverViews = 1024;

DWORD WINAPI hand_over_backstop(void* event) {
    Sleep(kHandOverBackstopMs);
    SetEvent(static_cast<HANDLE>(event));
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

void hand_over(HANDLE event, const std::wstring& cooked) {
    log_line("host:", "ending: the new process takes over");
    std::fflush(stdout);
    std::fflush(stderr);
    // Before the stop: every mapped view's base, and those of files under .cooked, by the path the system names them
    // with (\Device\...\mods\.cooked\...): the folder's path past its drive is found in it, letters in any case.
    static const void* mapped[kHandOverViews];
    static const void* cooked_views[kHandOverViews];
    size_t mapped_count = 0;
    size_t cooked_count = 0;
    const wchar_t* const tail = cooked.size() > 2u && cooked[1] == L':' ? cooked.c_str() + 2 : cooked.c_str();
    const size_t tail_length = std::wcslen(tail);
    static wchar_t name[MAX_PATH * 2];
    MEMORY_BASIC_INFORMATION region{};
    for (const char* at = nullptr; VirtualQuery(at, &region, sizeof(region)) == sizeof(region);
         at = static_cast<const char*>(region.BaseAddress) + region.RegionSize) {
        if (region.Type != MEM_MAPPED || region.AllocationBase != region.BaseAddress) continue;
        if (mapped_count < kHandOverViews) mapped[mapped_count++] = region.BaseAddress;
        if (tail_length == 0u || cooked_count == kHandOverViews) continue;
        const DWORD length = GetMappedFileNameW(GetCurrentProcess(), region.BaseAddress, name, MAX_PATH * 2);
        for (DWORD i = 0; length >= tail_length && i + tail_length <= length; i++) {
            if (_wcsnicmp(name + i, tail, tail_length) == 0) {
                cooked_views[cooked_count++] = region.BaseAddress;
                break;
            }
        }
    }
    DWORD backstop_id = 0;
    const HANDLE backstop = CreateThread(nullptr, 64 * 1024, hand_over_backstop, event, 0, &backstop_id);
    const DWORD self = GetCurrentThreadId();
    const DWORD process = GetCurrentProcessId();
    // Twice: a thread the first pass missed may have started one. A thread stopped twice stays stopped.
    for (int pass = 0; pass < 2; pass++) {
        const HANDLE threads = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (threads == INVALID_HANDLE_VALUE) continue;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Thread32First(threads, &entry); more; more = Thread32Next(threads, &entry)) {
            if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self ||
                (backstop != nullptr && entry.th32ThreadID == backstop_id)) {
                continue;
            }
            if (const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID)) {
                SuspendThread(thread);
                CloseHandle(thread);
            }
        }
        CloseHandle(threads);
    }
    gPak.store.let_go_of_files();
    for (size_t i = 0; i < cooked_count; i++) UnmapViewOfFile(cooked_views[i]);
    // A view mapped since the count: not named (see above), unmapped as it may be one of .cooked's. With more views
    // than the count holds, the rest are left (only .cooked's, named above, matter).
    if (mapped_count < kHandOverViews) {
        for (const char* at = nullptr; VirtualQuery(at, &region, sizeof(region)) == sizeof(region);
             at = static_cast<const char*>(region.BaseAddress) + region.RegionSize) {
            if (region.Type != MEM_MAPPED || region.AllocationBase != region.BaseAddress) continue;
            bool known = false;
            for (size_t i = 0; i < mapped_count && !known; i++) known = mapped[i] == region.BaseAddress;
            if (!known) UnmapViewOfFile(region.BaseAddress);
        }
    }
    if (event != nullptr) SetEvent(event);
}

// The new process's half: before it touches the mods folder it waits, at most 10 s, for the old one's event or its end,
// whichever comes first, and says how long in the log and as the harness fact host.handover_ms. Without the event (an
// old process of before it) or past the bound, it waits for the old process to end instead, at most 60 s, said. Then a
// thread says when the old process ended. The id is checked against its creation time, so a process that took
// a freed id is never waited for.
void wait_for_hand_over() {
    const std::string& words = ::galaxy::launch_args().start_again_of;
    const size_t dash = words.find('-');
    if (dash == std::string::npos) return;
    unsigned long pid = 0;
    unsigned long long created = 0;
    try {
        pid = std::stoul(words.substr(0, dash));
        created = std::stoull(words.substr(dash + 1));
    } catch (const std::exception&) {
        return;
    }
    const auto began = std::chrono::steady_clock::now();
    const auto since = [&began] {
        return static_cast<long long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count());
    };
    HANDLE before = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (before != nullptr && creation_of(before) != created) {
        CloseHandle(before);
        before = nullptr;  // another process took the id: the old one has ended
    }
    const HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, hand_over_event_name(pid, created).c_str());
    DWORD came = WAIT_TIMEOUT;
    if (event != nullptr && before != nullptr) {
        const HANDLE either[2] = {event, before};
        came = WaitForMultipleObjects(2, either, FALSE, 10000);
    }
    if (event != nullptr) CloseHandle(event);
    std::string said;
    if (came == WAIT_OBJECT_0) {
        said = "the process before it handed over its files after " + std::to_string(since()) + " ms";
    } else if (came == WAIT_OBJECT_0 + 1) {
        said = "the process before it ended without a hand-over after " + std::to_string(since()) + " ms";
    } else if (before != nullptr) {
        said = event == nullptr ? "no hand-over from the process before it; " : "the hand-over did not come within 10 s; ";
        said += WaitForSingleObject(before, 60000) == WAIT_OBJECT_0
                    ? "waited " + std::to_string(since()) + " ms for it to end"
                    : "it had not ended after 60 s; this one goes on";
    } else {
        said = "the process before it had ended";
    }
    const long long waited = since();
    log_line("host: started again:", said.c_str());
    galaxy::modlog::fact("host.handover_ms", std::to_string(waited));
    if (before == nullptr) return;
    // When the old process ended, from this one's start: the system's part of a start-again, said once it is known.
    const unsigned long long mine = creation_of(GetCurrentProcess());
    std::thread([before, mine] {
        if (WaitForSingleObject(before, 120000) == WAIT_OBJECT_0) {
            FILETIME made{}, ended{}, kernel{}, user{};
            if (GetProcessTimes(before, &made, &ended, &kernel, &user)) {
                const unsigned long long end = (static_cast<unsigned long long>(ended.dwHighDateTime) << 32u) | ended.dwLowDateTime;
                const long long ms = end > mine ? static_cast<long long>((end - mine) / 10000u) : 0;
                log_line("host: started again:", ("the process before it ended " + std::to_string(ms) +
                                                   " ms after this one started").c_str());
            }
        }
        CloseHandle(before);
    }).detach();
}
#endif

#if defined(_WIN32)
// A start-again's hand-over, set up before the new process starts (finish_and_exit).
HANDLE gHandOver = nullptr;
std::wstring gHandOverCooked;
// The program a start-again runs in this one's place: empty, this program (finish_and_exit); another renderer's
// program (start_again_on, renderer_restart_program).
std::wstring gStartAgainProgram;
#endif

#if defined(_WIN32) && defined(_M_X64) && defined(NEBULA_HOST_RENDERER)
const char* renderer_title(galaxy::render::RendererKind kind) {
    switch (kind) {
    case galaxy::render::RendererKind::kD3D12: return "Direct3D 12";
    case galaxy::render::RendererKind::kD3D11: return "Direct3D 11";
    case galaxy::render::RendererKind::kVulkan: return "Vulkan";
    default: return "the automatic renderer";
    }
}

// The program of `kind` of this program's CPU level beside it (renderer_check.h renderer_program); empty, said in the
// log with `why`, when it is not there.
std::wstring program_of(galaxy::render::RendererKind kind, const char* why) {
    std::wstring path = galaxy::render::renderer_program(kind).wstring();
    if (path.empty()) {
        log_line((std::string("host: ") + renderer_title(kind) + " is wanted and its program is not beside the game:").c_str(), why);
    }
    return path;
}

// No card runs this program's renderer (render::found_no_device), known only once the start has chosen the game
// module and read the mods: the program of the next renderer down kRendererPreference that a card runs (the renderer
// check, else Direct3D 11) takes over as a start-again does (finish_and_exit), the new process waiting for this one's
// hand-over before it touches the mods folder. A renderer the settings chose that no card runs is forgotten, so the
// next start takes the automatic choice. Returns when there is no renderer to fall back to or its program is not here.
void start_again_on_another_renderer(const char* why) {
    using galaxy::render::RendererKind;
    const RendererKind failed = galaxy::render::running_renderer();
    const std::vector<galaxy::render::CardRenderers> cards = galaxy::render::check_renderers_now(15000u);
    galaxy::render::CardRenderers assumed;  // the check did not answer: Direct3D 11, as before the check
    assumed.set(RendererKind::kD3D11, galaxy::render::RendererRefusal::kNone);
    const RendererKind next =
        galaxy::render::fallback_renderer(failed, cards.empty() ? assumed : galaxy::render::renderers_for(cards, {}));
    if (next == RendererKind::kAuto) {
        return;
    }
    std::wstring path = program_of(next, why);
    if (path.empty()) {
        return;
    }
    if (galaxy::render::next_start_renderer() == failed) {
        (void)galaxy::render::choose_next_start_renderer(RendererKind::kAuto);
    }
    log_line((std::string("host: ") + renderer_title(next) + " takes the start:").c_str(), why);
    gStartAgainProgram = std::move(path);
    finish_and_exit(true);
}

// The program a restart runs: where the player chose another renderer in this session (the settings' next-start
// renderer is not the one this start read), that renderer's program, or for the automatic choice the Direct3D 12
// program that makes it; else this program (empty).
std::wstring renderer_restart_program() {
    using galaxy::render::RendererKind;
    const RendererKind next = galaxy::render::next_start_renderer();
    if (::galaxy::launch_args().renderer != 0u ||
        static_cast<unsigned>(next) == galaxy::render::get_render_config().renderer) {
        return {};
    }
    const RendererKind program = next == RendererKind::kAuto ? RendererKind::kD3D12 : next;
    if (program == galaxy::render::running_renderer() && next != RendererKind::kAuto) {
        return {};
    }
    return program_of(program, "a restart with the renderer the settings chose");
}
#endif

#if defined(_WIN32) && defined(_M_X64) && defined(NEBULA_HOST_D3D12)
// The renderer the start was asked for (--renderer) or the settings chose, when it is not Direct3D 12: its program runs
// in this one's place before this one takes anything, the one-game lock, the log file, the safe start, the game
// module's choice, the mods folder and the window all its own (nebula_run_sibling: same command line, startup
// information, handles, environment and folder, in a job, waited for); this process ends with its exit code, so a
// caller sees one lifetime and one code. The other renderers' hosts have no hand-off, so the chain ends there. Returns
// when that program is not there or did not start.
void hand_off_to(galaxy::render::RendererKind kind, const char* why) {
    const std::wstring path = program_of(kind, why);
    if (path.empty()) {
        return;
    }
    log_line((std::string("host: ") + renderer_title(kind) + " takes the start:").c_str(), why);
    std::fflush(stdout);
    std::fflush(stderr);
    unsigned long code = 1u;
    if (!nebula_run_sibling(path.c_str(), &code)) {
        log_line((std::string("host: the ") + renderer_title(kind) + " program did not start:").c_str(), why);
        return;
    }
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
}
#endif

[[noreturn]] void finish_and_exit(bool restart) {
#if defined(_WIN32)
    game_profile::finish();
#endif
#if defined(NEBULA_HOST_RENDERER)
    galaxy::bench::at_exit();  // a benchmark that did not finish writes its result as aborted
    // The process ends without the renderer's shutdown: the pictures handed to an extension (a test's) are taken from
    // the GPU and written first, as that shutdown would.
    if (galaxy::extensions::keeps_pictures() && !galaxy::render::finish_pictures(5000u)) {
        log_line("host:", "the pictures asked were not all written at exit");
    }
#endif
#if defined(NEBULA_HOST_RENDERER)
    // --diagnostics pipelines: the pipelines this run created (tools/dev/site_pipelines.py merges them).
    if (::galaxy::diagnosing(::galaxy::kDiagnosePipelines)) {
        if (const fs::path folder = galaxy::nebula_diagnostics_directory(); !folder.empty()) {
            galaxy::render::registries().write_pipeline_list(
                folder / ("pipelines-" + std::to_string(galaxy::current_process_id()) + ".bin"));
        }
    }
#endif
    log_line("host:", restart ? "restart" : "the game ended");
    // The room hears this player go (a host's closes for everyone), as the process ends without its destructors.
    gCoopSession.leave();
    {
        std::unique_lock<std::mutex> lock(gSavesMutex);
        gSavesDone.wait_for(lock, std::chrono::seconds(5), [] { return gSavesPending.load(std::memory_order_acquire) == 0; });
    }
    if (gSavesPending.load(std::memory_order_acquire) != 0) {
        log_line("host:", "a save still pending at exit");
    }
#if defined(NEBULA_HOST_RENDERER)
    galaxy::achievements::flush();
    galaxy::stats::flush();
#endif
    // The record of mods' numbers (services/mods.md 7), written off the game thread.
    galaxy::luma_mods::flush_ids(2000u);
    // The extensions' files complete (a test's sound dump); the start that takes over is told it is one, by its count.
    galaxy::extensions::ending(restart);
    if (restart) {
        // The new start counts this one (launch_args.h started_again; a later value wins).
        add_restart_argument("--started-again");
        add_restart_argument(std::to_string(::galaxy::launch_args().started_again + 1u));
    }
#if defined(NEBULA_UPDATE_CHECK)
    galaxy::update::finish();  // a check still running: its answer kept for the next start (at most its timeout)
#endif
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
    // Windows HDR the game turned on goes off again; a restart hands it to the next process (render/os_hdr.h).
    galaxy::render::os_hdr::finish(restart);
#endif
    std::fflush(stdout);
#if defined(_WIN32)
    if (restart) {
#if defined(NEBULA_HOST_RENDERER)
        if (gOneGame != nullptr) {
            CloseHandle(gOneGame);  // the new process takes it
        }
        if (const unsigned instance = galaxy::game_instance(); instance > 1u) {
            SetEnvironmentVariableW(kGameInstanceVariable, std::to_wstring(instance).c_str());
        }
#endif
#if defined(_M_X64) && defined(NEBULA_HOST_RENDERER)
        if (gStartAgainProgram.empty()) {
            gStartAgainProgram = renderer_restart_program();
        }
#endif
        const std::wstring path = gStartAgainProgram.empty() ? ::galaxy::windows::executable_path() : gStartAgainProgram;
        if (!path.empty()) {
            // The new process writes to this one's output and error, when it has them (a tool reading its log), and
            // inherits nothing else.
            HANDLE pass[2] = {};
            DWORD passed = 0;
            for (const DWORD which : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
                const HANDLE handle = GetStdHandle(which);
                if (handle == nullptr || handle == INVALID_HANDLE_VALUE || GetFileType(handle) == FILE_TYPE_UNKNOWN) continue;
                if (passed == 1u && pass[0] == handle) continue;
                if (SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) pass[passed++] = handle;
            }
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            if (::galaxy::launch_args().started_hidden) {
                // A hidden start (a test's) stays hidden: the new process reads it from its own start the same way.
                startup.StartupInfo.dwFlags |= STARTF_USESHOWWINDOW;
                startup.StartupInfo.wShowWindow = SW_HIDE;
            }
            std::vector<unsigned char> attributes;
            if (passed != 0u) {
                SIZE_T bytes = 0;
                InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
                attributes.resize(bytes);
                startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
                if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes) ||
                    !UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, pass, passed * sizeof(HANDLE), nullptr, nullptr)) {
                    passed = 0;
                    startup.lpAttributeList = nullptr;
                } else {
                    startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
                    startup.StartupInfo.hStdInput = nullptr;
                    startup.StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
                    startup.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
                }
            }
            PROCESS_INFORMATION process{};
            std::wstring command = GetCommandLineW();
            command += gRestartArguments;
#if defined(NEBULA_HOST_RENDERER)
            if (galaxy::render::native_settings_launch_imports_taken()) command += L" --imports-taken 1";
#endif
            // The new process waits for this one's hand-over before it touches the mods folder (wait_for_hand_over):
            // this one's id and creation time (--start-again-of, a later value wins), and the event named for them.
            if (const unsigned long long when = creation_of(GetCurrentProcess()); when != 0u) {
                command += L" --start-again-of " + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(when);
                gHandOver = CreateEventW(nullptr, TRUE, FALSE, hand_over_event_name(GetCurrentProcessId(), when).c_str());
                std::error_code whole;
                fs::path cooked = fs::absolute(galaxy::mods::mods_folder() / ".cooked", whole);
                if (!whole && !galaxy::mods::mods_folder().empty()) gHandOverCooked = cooked.make_preferred().wstring();
            }
            log_line("host:", "handing over to a new process");
            if (CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, passed != 0u ? TRUE : FALSE,
                               passed != 0u ? EXTENDED_STARTUPINFO_PRESENT : 0u, nullptr, nullptr, &startup.StartupInfo, &process)) {
                // Where crash reports go (--crash-dumps), `restarted` names the process that took over, so a tool
                // waiting for this one follows it: its id and its creation time (FILETIME as one number), so an id the
                // system gives again to another process later is never taken for it.
                if (const fs::path dumps = ::galaxy::launch_args().crash_dumps; !dumps.empty()) {
                    FILETIME created{}, ended{}, kernel{}, user{};
                    if (GetProcessTimes(process.hProcess, &created, &ended, &kernel, &user)) {
                        const unsigned long long when = (static_cast<unsigned long long>(created.dwHighDateTime) << 32u) | created.dwLowDateTime;
                        std::ofstream(dumps / "restarted", std::ios::binary | std::ios::trunc) << process.dwProcessId << ' ' << when << "\n";
                    }
                }
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }
            if (startup.lpAttributeList != nullptr) DeleteProcThreadAttributeList(startup.lpAttributeList);
        }
    }
#elif defined(__ANDROID__) && defined(NEBULA_HOST_RENDERER)
    if (restart) {
        // The activity ends this process once the new one is on its way.
        galaxy::render::host_window::start_again(static_cast<galaxy::render::host_window::Restart>(gRestartKind));
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
#elif !(defined(__APPLE__) && TARGET_OS_IPHONE)
    if (restart && gArgv != nullptr) {
        // This process becomes the game again (exec: the same process id, so a tool waiting for it follows it as it
        // is), the running executable with this start's arguments and the ones a restart adds (add_restart_argument).
        // Every C stream is written out first; every file this start opened (the pak, the log, the facts, the sound
        // dump) is closed by the exec itself, only standard input, output and error stay.
        std::fflush(nullptr);
#if defined(__APPLE__)
        std::uint32_t size = 0u;
        _NSGetExecutablePath(nullptr, &size);
        std::string exe(size, '\0');
        if (_NSGetExecutablePath(exe.data(), &size) != 0) exe.clear();
        const char* const folder = "/dev/fd";
#else
        std::string exe = "/proc/self/exe";
        // An AppImage: the image file itself, which mounts itself again (the program in the old mount goes with the
        // mount, which ends when the descriptors closed at the exec do).
        if (const char* image = std::getenv("APPIMAGE"); image != nullptr && image[0] == '/') exe = image;
        const char* const folder = "/proc/self/fd";
#endif
        std::vector<char*> arguments;
        for (char** argument = gArgv; *argument != nullptr; ++argument) arguments.push_back(*argument);
#if defined(NEBULA_HOST_START_AGAIN)
        for (std::string& added : gRestartExtra) arguments.push_back(added.data());
#endif
#if defined(NEBULA_HOST_RENDERER)
        static char imports_taken_key[] = "--imports-taken";
        static char imports_taken_value[] = "1";
        if (galaxy::render::native_settings_launch_imports_taken()) {
            arguments.push_back(imports_taken_key);
            arguments.push_back(imports_taken_value);
        }
#endif
        arguments.push_back(nullptr);
        std::error_code error;
        std::vector<int> open;
        for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
            const std::string name = entry.path().filename().string();
            char* end = nullptr;
            const long fd = std::strtol(name.c_str(), &end, 10);
            if (end != name.c_str() && *end == '\0' && fd > 2) open.push_back(static_cast<int>(fd));
        }
        for (const int fd : open) {
            const int flags = fcntl(fd, F_GETFD);
            if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
        if (!exe.empty()) execv(exe.c_str(), arguments.data());
        log_line("host:", ("the game could not start again: " + std::string(std::strerror(errno))).c_str());
        std::fflush(stdout);
    }
#else
    (void)restart;  // an iOS app cannot replace its own image: kHostStartsAgain is false there
#endif
#if defined(_WIN32)
#if defined(NEBULA_HOST_RENDERER)
    galaxy::host::stop_all_rumble();
#endif
    // Saves and logs are out. No library's detach runs: Streamline's faults on state from before a lost card.
    if (restart && gHandOver != nullptr) hand_over(gHandOver, gHandOverCooked);
    TerminateProcess(GetCurrentProcess(), 0);
#endif
    std::_Exit(0);
}

void svc_app_exit(void*) {
#if defined(NEBULA_HOST_RENDERER)
#if defined(NEBULA_UPDATE_CHECK) && defined(_WIN32)
    // The notice's Update: Nebula Setup installs it once the game has left.
    if (galaxy::ui::launcher_poll() == galaxy::ui::LauncherModel::kChoiceUpdate && !galaxy::update::start_setup_update()) {
        log_line("host:", "Nebula Setup could not start to install the update");
    }
#endif
#endif
    finish_and_exit(false);
}

// The layers mods' code chooses (luma.h LumaModInfo.layers). The content is cooked with the record
// (galaxy::mods::code_layers_record); an answer that differs from it starts the game again once.
// gStartedAgainForLayers: this process was started again for layers (the record's restart file was there at the
// start), so it never starts again for them: no loop is possible. A background cook of this start waits for the
// answer (gLayersWait) and is planned with it, so a new mod gets one start-again, not two.
bool gStartedAgainForLayers = false;
std::mutex gLayersMutex;
std::condition_variable gLayersAnswered;
bool gLayersWait = false;      // a background cook waits for the answer
bool gLayersAnswer = false;    // the answer is in (or none will come)
std::optional<galaxy::mods::ModPlan> gLayersPlan;  // the plan made with a changed answer
// The start's scan, its game and language, kept to plan again with a changed answer.
std::vector<galaxy::mods::ModInfo> gStartMods;
std::string gStartGameId;
std::string gStartLanguage;

// Ends the background cook's wait: with a plan made from a changed answer, or with none (cook as planned).
void layers_answered(std::optional<galaxy::mods::ModPlan> plan) {
    {
        const std::lock_guard<std::mutex> lock(gLayersMutex);
        if (gLayersAnswer) return;
        gLayersAnswer = true;
        gLayersPlan = std::move(plan);
    }
    gLayersAnswered.notify_all();
}

// start_plugins' choose: every begun mod's answer, compared with the record. Only a mod's own code layers count; any
// other name is said once in the mod log. Equal: the start goes on.
void layers_chosen(const std::vector<galaxy::mods::ChosenLayers>& chosen) {
    const galaxy::mods::CodeLayers record = galaxy::mods::code_layers_record();
    galaxy::mods::CodeLayers next = record;
    std::vector<std::string> changed;
    for (const galaxy::mods::ChosenLayers& one : chosen) {
        const galaxy::mods::PluginFile& plugin = *one.plugin;
        std::vector<std::string> said;
        for (const std::string& name : one.names) {
            const bool known = std::find(plugin.code_layers.begin(), plugin.code_layers.end(), name) != plugin.code_layers.end();
            if (known || std::find(said.begin(), said.end(), name) != said.end()) continue;
            said.push_back(name);
            log_line("mods:", (plugin.mod_id + ": its code chose the layer \"" + name + "\", which is not one of its code layers; it is left out").c_str());
            galaxy::modlog::add_row(3u, 0u, plugin.mod_id, 170u, "luma.json", "Layers.Unknown", {{"layer", name}});
        }
        if (plugin.code_layers.empty()) continue;  // nothing to choose: no record, never a start-again
        // In the manifest's order, each once, so the same choice always reads the same.
        std::vector<std::string> keep;
        for (const std::string& name : plugin.code_layers) {
            if (std::find(one.names.begin(), one.names.end(), name) != one.names.end()) keep.push_back(name);
        }
        const auto was = record.find(plugin.mod_id);
        if (was != record.end() ? was->second != keep : !keep.empty()) changed.push_back(plugin.mod_id);
        next[plugin.mod_id] = std::move(keep);
    }
    if (changed.empty()) {
        layers_answered(std::nullopt);
        return;
    }
    if (gStartedAgainForLayers) {
        // Started again for these layers, and the answer changed again: the cooked layers stay for this session.
        for (const std::string& mod : changed) {
            log_line("mods:", (mod + ": its code chose other layers again after the game started again for them; the cooked ones stay").c_str());
            galaxy::modlog::add_row(3u, 0u, mod, 171u, "", "Layers.Unstable", {});
        }
        layers_answered(std::nullopt);
        return;
    }
    if (!galaxy::mods::write_code_layers_record(next)) {
        log_line("mods:", "the layers the mods' code chose could not be written: the cooked ones stay");
        layers_answered(std::nullopt);
        return;
    }
    bool waits = false;
    {
        const std::lock_guard<std::mutex> lock(gLayersMutex);
        waits = gLayersWait;
    }
    if (waits) {
        // This start cooks in the background: that cook takes the new layers, and its start-again follows.
        log_line("mods:", "the mods' code chose other layers: the files being prepared use them");
        layers_answered(galaxy::mods::plan_mod_changes(gPak.store, gStartGameId, gStartLanguage, gStartMods));
        return;
    }
    std::string ids;
    for (const std::string& mod : changed) ids += (ids.empty() ? "" : ", ") + mod;
    if constexpr (!kHostStartsAgain) {
        // This host cannot start the game again (finish_and_exit): the record is written, so the next start cooks with
        // the new layers; this one goes on with the cooked ones.
        log_line("mods:", ("the code of " + ids + " chose other layers than the content was cooked with: they are used from the next start").c_str());
        layers_answered(std::nullopt);
        return;
    }
    log_line("mods:", ("the code of " + ids + " chose other layers than the content was cooked with: the game starts again with them").c_str());
    std::ofstream(galaxy::mods::code_layers_restart_file(), std::ios::binary) << "layers\n";
    finish_and_exit(true);
}

void svc_mods_start(void*) {
    // A background cook waiting for the layers' answer is let go however this ends (no code, a crash flag, no answer).
    struct AnswerAtEnd {
        ~AnswerAtEnd() { layers_answered(std::nullopt); }
    } answer_at_end;
    const fs::path crashed = galaxy::mods::mods_folder() / ".code_crashed";
    std::error_code crash_error;
    if (fs::exists(crashed, crash_error)) {
        fs::remove(crashed, crash_error);
        if (!gPlugins.empty()) {
            // A dev mod's code stays on: its maker is working on that crash (mods.h dev_mods).
            log_line("mods:", galaxy::mods::dev_mods().empty() ? "the last run crashed with mods' code on: it stays off for this run"
                                                                : "the last run crashed with mods' code on: it stays off for this run, but for the dev mods");
            std::erase_if(gPlugins, [](const galaxy::mods::PluginFile& p) { return !galaxy::mods::dev_mod(p.mod_id); });
            galaxy::mods::set_code_off_after_crash();
        }
    }
    // The plain module has no rows: no mod's code starts with it. The choice at start saw no such mod, or the crash flag
    // above turned its code off; one here means the mods changed between that look and this start,
    // and its code waits for the next start.
    if (galaxy::game_module::plain_loaded() && !gPlugins.empty()) {
        log_line("mods:", "a mod with code was turned on after the game module was chosen: its code starts at the next start");
        gPlugins.clear();
    }
    // The mod log (galaxy/mod_log.h): how many mods are on, and the harness's facts in a harness run. With none it is
    // never made.
    galaxy::modlog::begin(
        [](const char* name) -> void* {
#if defined(_WIN32)
            return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(gModule.module), name));
#else
            return dlsym(gModule.module, name);
#endif
        },
        gModsOn);
    // The game sounds mods replace (sound.md 6; the audio service), code or none. A module without the replacer is
    // left alone.
    galaxy::luma_audio::set_replacements([](const char* name) -> void* {
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(gModule.module), name));
#else
        return dlsym(gModule.module, name);
#endif
    });
    // What the start's check and the cook said, in the mod log: after the mods' code started, or here with no mod that
    // changes the game itself (texture pack pictures or patches alone).
    const auto log_the_start = [] {
        // The start check's one line when a part was missing (every mod off this session).
        if (gMissingPart) galaxy::startcheck::report(*gMissingPart);
        // Each mod whose code did not start, in the mod log as the Mods page says it.
        galaxy::modlog::add_not_started();
        // Each placed package through the validator, its findings in the mod log (kept: nothing runs when nothing
        // changed).
        galaxy::modlog::add_start_findings();
        // The cooker's facts for the store attached this run: its refusals and notes, and in a harness run what it
        // cooked.
        galaxy::modlog::add_cook_facts(gCookFacts, gCookOwners);
    };
    // Any mod at all tells the game: a stage then holds more than its level asks for.
    if (!gModsChangeGame) {
        log_the_start();
        return;
    }
    // A crash from here with code on leaves the code off at the next start; without, every mod (set at the scan).
    if (!gPlugins.empty()) {
        gCodeCrashFile = crashed;
    }
    const auto game_api = reinterpret_cast<const NebulaModApi* (*)()>(find_symbol(gModule.module, "nebula_game_mod_api"));
#if defined(NEBULA_HOST_RENDERER)
    galaxy::mods::set_achievement_store(galaxy::achievements::unlock_mod);
#endif
#if defined(NEBULA_HOST_RENDERER)
    galaxy::mods::set_message_language(galaxy::mods::language_folder(
        std::string(gPak.store.game_id()),
        static_cast<unsigned>(galaxy::game_language_resolved(galaxy::get_runtime_settings().language))));
#endif  // a headless host has no language setting: refusals stay in English, the default
    galaxy::mods::start_plugins(gPlugins, gModRows, game_api(),
                                [](const char* name) -> void* {
#if defined(_WIN32)
                                    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(gModule.module), name));
#else
                                    return dlsym(gModule.module, name);
#endif
                                },
                                [](const char* text) { log_line("mods:", text); }, layers_chosen);
    // The characters the mods added, for the Options page's CHARACTER row (the game's list, Mario and Luigi first).
    if (const NebulaModApi* api = game_api(); api != nullptr && api->character_like != nullptr) {
        std::vector<galaxy::PlayableCharacter> characters;
        for (int32_t i = 0; i < api->character_count(); ++i) {
            const char* id = api->character_id(i);
            const char* name = api->character_name(i);
            if (id == nullptr) continue;
            galaxy::PlayableCharacter character{id, name != nullptr ? name : id,
                                                api->character_like(i) == 1 ? galaxy::CoopCharacter::Luigi : galaxy::CoopCharacter::Mario};
            // Its own colours, for the page's COLOURS rows.
            for (int32_t part = 0; part < NEBULA_PALETTE_PARTS && api->character_palette_color != nullptr; ++part) {
                const int32_t rgb = api->character_palette_color(i, part);
                if (rgb < 0) continue;
                character.palette[static_cast<std::size_t>(part) * 3] = static_cast<std::uint8_t>(rgb >> 16);
                character.palette[static_cast<std::size_t>(part) * 3 + 1] = static_cast<std::uint8_t>(rgb >> 8);
                character.palette[static_cast<std::size_t>(part) * 3 + 2] = static_cast<std::uint8_t>(rgb);
            }
            characters.push_back(std::move(character));
        }
        galaxy::set_playable_characters(std::move(characters));
    }
    // After the last mod's start, before the title (luma_refs.h), on the game thread: each placement of a class only
    // code makes, from the start's check, looked up in the game's factory now that the mods' code has made its classes.
    // One nobody makes is named in the mod log and placed as the placeholder object. With no mod a player turned on
    // (the harness's own game half alone) it is not called.
    if (gPlayersModsOn != 0) {
#if defined(_WIN32)
        const auto check = reinterpret_cast<LumaRefsCheckAfterStart>(
            GetProcAddress(static_cast<HMODULE>(gModule.module), "luma_refs_check_after_start"));
#else
        const auto check = reinterpret_cast<LumaRefsCheckAfterStart>(dlsym(gModule.module, "luma_refs_check_after_start"));
#endif
        if (check != nullptr) {
            const std::vector<galaxy::modlog::AfterStartRow> rows = galaxy::modlog::after_start_rows();
            std::vector<LumaAfterStartRef> refs;
            refs.reserve(rows.size());
            for (const galaxy::modlog::AfterStartRow& row : rows) {
                refs.push_back({row.mod.c_str(), row.class_name.c_str(), row.file.c_str(), row.line});
            }
            check(refs.empty() ? nullptr : refs.data(), static_cast<uint32_t>(refs.size()));
        } else {
            log_line("mods:", "the game module has no luma_refs_check_after_start: placements of classes only code makes are not checked");
        }
    }
    log_the_start();
}

void svc_app_restart(void*) {
    finish_and_exit(true);
}


// The mod tools (run_mods_tool): the first argument names one.
bool is_mods_tool(std::string_view first) {
    return first == "--import-mod" || first == "--list-mods" || first == "--remove-mod";
}

// The launch arguments a mods tool takes too: the folders it works in.
bool is_tool_folder_option(std::string_view key) {
    return key == "--data" || key == "--settings" || key == "--crash-dumps" || key == "--shipped-mods";
}

}  // namespace


#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/mod_import.h"

#if defined(__APPLE__)
// The importer, linked into the program on Apple (platform/apple/metal.cmake; crates/nebula-import/src/jni.rs).
extern "C" int nebula_cook_overlay(const char* plan, const char* output);
#endif

namespace {

std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20u) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
            out += escaped;
        } else {
            out.push_back(c);
        }
    }
    return out + "\"";
}


// The game's language folders: the disc's, and the language packs' among the mods on at the start.
std::vector<std::string> gGameLanguages;

// The mods' disc files, cooked once into a store the game reads over its own (cook/overlay.rs): by the cooker beside
// this program, again when what they change has changed. A failure leaves the game as it is and says why in the log.
void cook_mod_files(const galaxy::mods::ModPlan& plan, const fs::path& settings_folder, const std::string& game_id);
// `mods`: the start's scan (scan_mods(game_id) with the played language set).
void prepare_mod_files(const fs::path& settings_folder, const std::string& game_id, std::vector<galaxy::mods::ModInfo> mods) {
    const std::string language = galaxy::mods::language_folder(
        game_id, static_cast<unsigned>(galaxy::game_language_resolved(galaxy::get_runtime_settings().language)));
    // Started again because a mod's code chose other layers: read once and gone, so this start never starts again for
    // them.
    {
        std::error_code restart_error;
        const fs::path restarted = galaxy::mods::code_layers_restart_file();
        gStartedAgainForLayers = !restarted.empty() && fs::exists(restarted, restart_error);
        if (gStartedAgainForLayers) fs::remove(restarted, restart_error);
    }
    // Step 1 of a start with mods (mod_start_check.h): with a mod on, the game's own parts the mods need. One
    // missing: every mod is off this session, with one mod-log line; nothing of the mods runs. No mod on: no check.
    if (std::any_of(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& mod) { return mod.enabled; })) {
        // The cook writes about what the mods bring: their files' bytes are the estimate.
        std::uint64_t bring = 0;
        for (const galaxy::mods::ModInfo& mod : mods) {
            if (!mod.enabled) continue;
            std::error_code walk_error;
            for (fs::recursive_directory_iterator it(mod.path, walk_error), end; !walk_error && it != end; it.increment(walk_error)) {
                std::error_code size_error;
                if (it->is_regular_file(size_error)) bring += it->file_size(size_error);
            }
        }
        if (const std::optional<galaxy::startcheck::MissingPart> part =
                galaxy::startcheck::missing_part(gPak.store, settings_folder / "mods" / ".cooked", bring)) {
            log_line("mods:", ("a part a start with mods needs is missing (" + part->why + "): every mod is off this time").c_str());
            gMissingPart = part;  // said in the mod log once it exists (start_mods)
            for (galaxy::mods::ModInfo& mod : mods) {
                if (!is_harness_half(mod.id)) mod.enabled = false;
            }
        }
    }
    gStartPackages.clear();
    for (const galaxy::mods::ModInfo& mod : mods) {
        if (mod.package) gStartPackages.push_back({mod.id, mod.path, mod.enabled});
    }
    // The validator's errors refuse a package at each start as at import (one reading, validator.md 2); its findings go
    // to the mod log once the log exists (add_start_findings).
    galaxy::modlog::check_packages(gStartPackages, game_id, language);
    for (galaxy::mods::ModInfo& mod : mods) {
        if (mod.package && mod.problem.empty() && galaxy::modlog::package_refused(mod.id)) {
            mod.problem = galaxy::modlog::refused_sentence();
        }
    }
    gModsOn = static_cast<std::uint32_t>(
        std::count_if(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& mod) { return mod.enabled; }));
    gPlayersModsOn = static_cast<std::uint32_t>(std::count_if(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& mod) {
        return mod.enabled && !is_harness_half(mod.id);
    }));
    // The answers the plan's granted lists come from: a declined permission's L506 is then the player's choice
    // (consent.md 2.3).
    galaxy::modlog::keep_permission_answers(mods);
    // A mod whose code may choose layers: the scan is kept to plan again with a changed answer. No such mod, no copy.
    if (std::any_of(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& mod) {
            return mod.enabled && mod.luma &&
                   std::any_of(mod.luma->layers.begin(), mod.luma->layers.end(), [](const galaxy::luma::Layer& layer) { return layer.when.code; });
        })) {
        gStartMods = mods;
        gStartGameId = game_id;
        gStartLanguage = language;
    }
    const galaxy::mods::ModPlan plan = galaxy::mods::plan_mod_changes(gPak.store, game_id, language, std::move(mods));
    gPlugins = plan.plugins;
    gModRows = plan.rows;
    gModsChangeGame = !plan.files.empty() || !plan.merges.empty() || !plan.materials.empty() || !plan.plugins.empty() || !plan.rows.empty();
    if (!plan.save_folder.empty() && !gSaveDataPath.empty()) {
        // A clone starts from the player's save the first time, the rest starts new; the player's stays as it is.
        const fs::path mine = gSaveRoot / gSaveDataPath.substr(1);
        const fs::path theirs = plan.save_folder / gSaveDataPath.substr(1);
        const fs::path old_mine = gSaveRoot / gOldSaveDataPath.substr(1);
        const fs::path old_theirs = plan.save_folder / gOldSaveDataPath.substr(1);
        std::error_code save_error;
        if (!fs::exists(theirs, save_error)) {
            fs::create_directories(theirs, save_error);
            if (!plan.save_start.empty()) {
                fs::copy(plan.save_start, plan.save_folder, fs::copy_options::recursive | fs::copy_options::skip_existing, save_error);
            } else if (plan.save_clone) {
                fs::copy(mine, theirs, fs::copy_options::recursive | fs::copy_options::skip_existing, save_error);
                if (fs::exists(old_mine, save_error)) {
                    fs::create_directories(old_theirs, save_error);
                    fs::copy(old_mine, old_theirs, fs::copy_options::recursive | fs::copy_options::skip_existing, save_error);
                }
            }
        }
        gModSaveRoot = plan.save_folder;
        log_line("mods:", "saves go to the mod's save folder");
    }
    for (const std::string& note : plan.notes) {
        log_line("mods:", note.c_str());
    }
    cook_mod_files(plan, settings_folder, game_id);
#if defined(NEBULA_HOST_RENDERER)
    // draw.post's compiles write their scratch files beside the cooked mods (a test run keeps to its own folder).
    galaxy::render::set_mod_post_work_folder(settings_folder / "mods" / ".cooked");
#endif
    // What the .luma mods' materials need: what they brought, then what the game built for them.
    {
        std::error_code built_error;
        const fs::path built = settings_folder / "mods" / ".cooked" / "built.shaders";
        if (fs::is_regular_file(built, built_error)) {
            galaxy::render::load_mod_shaders(built);
        }
        // The flags program_table keeps for its own programs, for the mods' (the cook's, beside the texts).
        galaxy::render::load_mod_program_flags(settings_folder / "mods" / ".cooked" / "mods.sources" / "flags.tsv");
    }
    for (const fs::path& folder : plan.luma_folders) {
        std::error_code shaders_error;
        const fs::path shaders = folder / "shaders" / galaxy::render::program_file_name();
        if (fs::is_regular_file(shaders, shaders_error)) {
            const galaxy::render::ModShadersResult loaded = galaxy::render::load_mod_shaders(shaders);
            // Such a file adds nothing to what the cook found present: the shaders its models need are built here
            // (build_missing_mod_shaders, "shaders built here").
            if (loaded == galaxy::render::ModShadersResult::OtherInterface) {
                log_line("mods:", "its shaders are for another build of Nebula: the ones it needs are built here");
            } else if (loaded == galaxy::render::ModShadersResult::Unreadable) {
                log_line("mods:", "its shaders could not be read: the ones it needs are built here");
            }
        }
    }
}

// The cooker failed as a whole: why is in `<store>.why` (cook/overlay.rs), as it has no console to say it in.
void cook_failed(const fs::path& overlay) {
    fs::path why_file = overlay;
    why_file.replace_extension("why");
    std::ifstream in(why_file, std::ios::binary);
    std::string why((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!why.empty() && (why.back() == '\n' || why.back() == '\r')) why.pop_back();
    log_line("mods:", why.empty() ? "the mods' files could not be cooked: the game runs without them"
                                  : ("the mods' files could not be cooked: the game runs without them: " + why).c_str());
    // Its refusals are why: they reach the mod log as an attached cook's do (the cooker writes the file either way).
    gCookFacts = overlay;
    gCookFacts.replace_extension("facts");
}

// The cooker this build runs, by its file's size and time: another cooker (a new build, with its own applier and
// cooks) cooks the mods again. "none" when it is not found; the cook then fails and says so.
std::string cooker_identity() {
#if defined(_WIN32)
    const fs::path cooker = fs::path(::galaxy::windows::executable_path()).parent_path() / "nebula-import.exe";
#else
    fs::path cooker;
    if (void* const library = dlopen("libnebula_import.so", RTLD_NOW | RTLD_LOCAL)) {
        Dl_info info{};
        if (void* const cook = dlsym(library, "nebula_cook_overlay"); cook != nullptr && dladdr(cook, &info) != 0 && info.dli_fname != nullptr) {
            cooker = fs::path(info.dli_fname);
        }
    }
#endif
    std::error_code error;
    const auto size = fs::file_size(cooker, error);
    if (error) return "cooker none\n";
    const auto time = fs::last_write_time(cooker, error).time_since_epoch().count();
    return "cooker " + std::to_string(size) + " " + std::to_string(static_cast<long long>(time)) + "\n";
}

void cook_mod_files(const galaxy::mods::ModPlan& plan, const fs::path& settings_folder, const std::string& game_id) {
    // A mod of texture pack pictures alone is cooked too: the cooker reads each picture as the pack reader does.
    if (plan.files.empty() && plan.merges.empty() && plan.patches.empty() && plan.textures.empty()) {
        return;
    }
    // Each plan line that can make a cook fact, by the paths that fact names (its store path, or the mod's file as the
    // plan writes it), with its mod: a merge by its own file, as two mods' merges add to one game file.
    gCookOwners.clear();
    for (const galaxy::mods::ModPlan::File& file : plan.files) {
        if (!file.mod_id.empty()) gCookOwners.emplace_back(file.disc, file.mod_id);
    }
    for (const galaxy::mods::ModPlan::File& merge : plan.merges) {
        if (!merge.mod_id.empty()) gCookOwners.emplace_back(galaxy::path_text(merge.host), merge.mod_id);
    }
    for (const galaxy::mods::ModPlan::Patch& patch : plan.patches) {
        if (!patch.mod_id.empty()) gCookOwners.emplace_back(galaxy::path_text(patch.host), patch.mod_id);
    }
    for (const galaxy::mods::ModPlan::Patch& texture : plan.textures) {
        if (!texture.mod_id.empty()) gCookOwners.emplace_back(galaxy::path_text(texture.host), texture.mod_id);
    }
    const fs::path folder = settings_folder / "mods" / ".cooked";
    const fs::path overlay = folder / "mods.pak";
    const fs::path signature_file = folder / "mods.signature";
    // Another build's shader interface or shader compiler means their shaders are built again; another cooker, that
    // the mods are cooked again (table-keys.md 9: cached by the mod list, the game and the applier).
    const auto signature_of = [game_id](const galaxy::mods::ModPlan& planned) {
        return galaxy::mods::plan_signature(planned, game_id) + galaxy::render::program_interface() + " " +
               std::to_string(galaxy::render::mod_shader_compiler_revision()) + "\n" + cooker_identity();
    };
    const std::string signature = signature_of(plan);
    std::error_code error;
    fs::create_directories(folder, error);
    std::string kept;
    {
        std::ifstream in(signature_file, std::ios::binary);
        kept.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (kept != signature || !fs::is_regular_file(overlay, error)) {
        const auto cook_now = [folder, overlay, signature_file, game_id](const galaxy::mods::ModPlan& plan, const std::string& signature) -> bool {
        const fs::path plan_file = folder / "mods.plan";
        if (!galaxy::io::write_file_atomically(plan_file, galaxy::mods::plan_text(plan, game_id))) {
            log_line("mods:", "the plan could not be written");
            return false;
        }
        std::error_code cook_error;
        fs::remove(signature_file, cook_error);
#if defined(_WIN32)
        const fs::path cooker = fs::path(::galaxy::windows::executable_path()).parent_path() / "nebula-import.exe";
        std::wstring command = L"\"" + cooker.wstring() + L"\" overlay \"" + plan_file.wstring() + L"\" --output \"" +
                               overlay.wstring() + L"\"";
        PROCESS_INFORMATION process{};
        DWORD exit_code = 1;
        // It ends with the game however the game ends (windows_child.h); a start-again comes only after it ended.
        ::galaxy::windows::child_note().store([](const char* text) { log_line("mods:", text); });
        if (::galaxy::windows::start_child(cooker.c_str(), command, CREATE_NO_WINDOW, process) != ::galaxy::windows::ChildStart::NotStarted) {
            WaitForSingleObject(process.hProcess, INFINITE);
            GetExitCodeProcess(process.hProcess, &exit_code);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        } else {
            log_line("mods:", "the cooker (nebula-import) could not start");
        }
        if (exit_code != 0u) {
            cook_failed(overlay);
            return false;
        }
#elif defined(__APPLE__)
        // The importer is linked into the program (platform/apple/metal.cmake).
        if (nebula_cook_overlay(utf8_of(plan_file).c_str(), utf8_of(overlay).c_str()) != 0) {
            cook_failed(overlay);
            return false;
        }
#else
        // The importer's library.
        using CookOverlay = int (*)(const char*, const char*);
        void* const library = dlopen("libnebula_import.so", RTLD_NOW | RTLD_LOCAL);
        const auto cook = library != nullptr ? reinterpret_cast<CookOverlay>(dlsym(library, "nebula_cook_overlay")) : nullptr;
        if (cook == nullptr || cook(utf8_of(plan_file).c_str(), utf8_of(overlay).c_str()) != 0) {
            cook_failed(overlay);
            return false;
        }
#endif
        // What a mod holds that couldn't be used: the rest of it goes ahead.
        {
            fs::path left_out = overlay;
            left_out.replace_extension("left_out");
            std::ifstream lines(left_out);
            std::string line;
            while (std::getline(lines, line)) {
                log_line("mods: left out:", line.c_str());
            }
        }
        // What the patches by key did that a player must hear of (table-keys: conflicts, rows not there, selectors'
        // counts, patches left out), one fact a line, for the log here and the Mods screen.
        {
            fs::path facts = overlay;
            facts.replace_extension("facts");
            std::ifstream lines(facts);
            std::string line;
            while (std::getline(lines, line)) {
                log_line("mods: patch:", line.c_str());
            }
        }
        {
            // What their materials need and they didn't bring.
            std::vector<fs::path> brought;
            for (const fs::path& mod : plan.luma_folders) {
                brought.push_back(mod / "shaders" / galaxy::render::program_file_name());
            }
            fs::path sources = overlay;
            sources.replace_extension("sources");
            fs::path progress = overlay;
            progress.replace_extension("progress");
            const galaxy::render::ModShaderBuild built = galaxy::render::build_missing_mod_shaders(
                sources, brought, folder / "built.shaders", [progress](unsigned done, unsigned total) {
                    std::ofstream out(progress);
                    out << done << ' ' << total << '\n';
                });
            std::error_code progress_error;
            fs::remove(progress, progress_error);
            if (built.wanted != 0u) {
                char text[96];
                std::snprintf(text, sizeof(text), "%u of %u shaders built here", built.built, built.wanted);
                log_line("mods:", text);
                if (!built.problem.empty()) {
                    log_line("mods:", built.problem.c_str());
                }
            }
        }
        if (!galaxy::io::write_file_atomically(signature_file, signature)) {
            log_line("mods:", "the signature could not be written");
        }
            return true;
        };
#if defined(NEBULA_HOST_RENDERER)
        // The game's own screen while they cook; then it starts again with them, so that they are there from its first file.
        // A hidden test run cooks first, as its measurements start from the game with its mods. So does a start that is
        // itself the start-again for a mod's code layers: a second start-again for the same layers is never made.
        if ((!::galaxy::launch_args().started_hidden || (galaxy::extensions::flags() & NEBULA_EXT_MODS_SCREEN) != 0u) &&
            !gStartedAgainForLayers) {
            gModsProgressFile = overlay;
            gModsProgressFile.replace_extension("progress");
            gModsPreparing.store(2u, std::memory_order_release);
            log_line("mods:", "preparing their files");
            // A mod whose code chooses layers: this cook waits for that answer (svc_mods_start, the game thread) and
            // is planned with it when it changed, so the start-again that follows has the chosen layers.
            const bool waits = std::any_of(plan.plugins.begin(), plan.plugins.end(),
                                           [](const galaxy::mods::PluginFile& plugin) { return !plugin.code_layers.empty(); });
            if (waits) {
                const std::lock_guard<std::mutex> lock(gLayersMutex);
                gLayersWait = true;
            }
            std::thread([cook_now, signature_of, plan, signature, waits]() {
                galaxy::mods::ModPlan used = plan;
                std::string used_signature = signature;
                bool chosen = false;
                if (waits) {
                    std::unique_lock<std::mutex> lock(gLayersMutex);
                    if (!gLayersAnswered.wait_for(lock, std::chrono::minutes(2), [] { return gLayersAnswer; })) {
                        log_line("mods:", "the mods' code gave no layers in time: the files are prepared with the layers they had");
                    } else if (gLayersPlan) {
                        used = std::move(*gLayersPlan);
                        gLayersPlan.reset();
                        chosen = true;
                    }
                }
                if (chosen) used_signature = signature_of(used);
                if (cook_now(used, used_signature)) {
                    // Planned with this start's answer: the next start does not start again for layers.
                    if (chosen) std::ofstream(galaxy::mods::code_layers_restart_file(), std::ios::binary) << "layers\n";
                    finish_and_exit(true);
                }
                gModsPreparing.store(0u, std::memory_order_release);
            }).detach();
            return;
        }
#endif
        if (!cook_now(plan, signature)) {
            return;
        }
    }
    if (gPak.store.attach_overlay(overlay)) {
        gCookFacts = overlay;
        gCookFacts.replace_extension("facts");
        char text[96];
        std::snprintf(text, sizeof(text), "%zu files, %zu table or file merges and %zu patches by key applied", plan.files.size(),
                      plan.merges.size(), plan.patches.size());
        log_line("mods:", text);
    } else {
        log_line("mods:", "the mods' files could not be read: the game runs without them");
    }
}

// The game id from Setup's game.pak, read without mapping it; false (and nothing changed) for a file that is not one:
// the tools then work without it.
bool read_game_id(const fs::path& path, std::string& id) {
    std::string found = galaxy::assets::store_game_id(path);
    if (found.empty()) return false;
    id = std::move(found);
    return true;
}

// The mod tools: Nebula --import-mod <folder or archive>..., --remove-mod <id> or --list-mods
// [--game <id>] [--pak <game.pak>] [--data, --settings or --crash-dumps <folder>], on the mods folder beside the
// settings (as the game uses it; the folder options read_launch_args took). One JSON line on stdout; exit code 0 when
// everything succeeded.
int run_mods_tool() {
    std::vector<fs::path> args;
#if defined(_WIN32)
    int count = 0;
    if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) args.emplace_back(wide[i]);
        LocalFree(wide);
    }
#endif
    std::string game_id = "RMGE01";
    std::vector<fs::path> sources;
    fs::path pak;
    const bool list = !args.empty() && args[0] == "--list-mods";
    const bool remove = !args.empty() && args[0] == "--remove-mod";
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--game" && i + 1u < args.size()) {
            game_id = utf8_of(args[++i]);
        } else if (args[i] == "--pak" && i + 1u < args.size()) {
            std::string id;
            pak = args[++i];
            if (read_game_id(pak, id)) {
                game_id = id;
            }
        } else if (is_tool_folder_option(utf8_of(args[i])) && i + 1u < args.size()) {
            ++i;  // read_launch_args took it: not a mod to import
        } else {
            sources.push_back(args[i]);
        }
    }
    const fs::path settings_folder = galaxy::runtime_settings_folder();
    if (settings_folder.empty()) {
        std::printf("{\"ok\":false,\"message\":\"no settings folder\"}\n");
        return 2;
    }
    std::error_code error;
    fs::create_directories(settings_folder / "mods", error);
    galaxy::mods::set_mods_folder(settings_folder / "mods", settings_folder / "mods.ini", game_id);
    (void)galaxy::mods::install_builtin_mods();
    // The import's check reads the game's own files, as the game's does (a store that does not open: unchecked).
    galaxy::assets::AssetStore store;
    struct Unset {
        ~Unset() { galaxy::modlog::set_check_store(nullptr); }
    } unset;
    if (!pak.empty() && store.open(pak)) galaxy::modlog::set_check_store(&store);
    if (remove) {
        std::string message;
        const bool ok = !sources.empty() &&
                        galaxy::mods::remove_mod(settings_folder / "mods", utf8_of(sources.front()), &message);
        std::printf("{\"ok\":%s,\"message\":%s}\n", ok ? "true" : "false", json_text(message).c_str());
        return ok ? 0 : 1;
    }
    if (list) {
        // A library that comes with the game (LumaKit) is part of it: left out, as the game's Mods page leaves it out.
        fs::path shipped = ::galaxy::launch_args().shipped_mods;
        if (shipped.empty() && !galaxy::package_directory().empty()) shipped = galaxy::package_directory() / "luma" / "mods";
        galaxy::mods::note_shipped_mods(shipped, settings_folder / "mods");
        std::string out = "{\"ok\":true,\"folder\":" + json_text(utf8_of(settings_folder / "mods")) + ",\"mods\":[";
        bool first = true;
        for (const galaxy::mods::ModInfo& mod : galaxy::mods::scan_mods(game_id)) {
            if (galaxy::mods::built_in(mod)) continue;
            out += first ? "{" : ",{";
            first = false;
            out += "\"id\":" + json_text(mod.id) + ",\"enabled\":" + (mod.enabled ? "true" : "false") +
                   ",\"order\":" + std::to_string(mod.order) + ",\"riivolution\":" + (mod.riivolution ? "true" : "false") +
                   ",\"files\":" + ((mod.files || mod.loose) ? "true" : "false") + ",\"textures\":" +
                   (mod.textures ? "true" : "false") + ",\"movies\":" + (mod.movies ? "true" : "false") +
                   ",\"code_patches\":" + std::to_string(mod.code_patches) + ",\"problem\":" + json_text(mod.problem) +
                   ",\"languages\":[";
            for (std::size_t l = 0; l < mod.languages.size(); ++l) out += (l ? "," : "") + json_text(mod.languages[l]);
            out += "],\"options\":[";
            for (std::size_t o = 0; o < mod.options.size(); ++o) {
                const galaxy::mods::ModOption& option = mod.options[o];
                out += std::string(o ? "," : "") + "{\"key\":" + json_text(option.key) + ",\"section\":" +
                       json_text(option.section) + ",\"name\":" + json_text(option.name) +
                       ",\"selected\":" + std::to_string(option.selected) + ",\"choices\":[";
                for (std::size_t c = 0; c < option.choices.size(); ++c) {
                    const bool other = c < option.other_region.size() && option.other_region[c];
                    out += std::string(c ? "," : "") + "{\"name\":" + json_text(option.choices[c]) +
                           ",\"other_region\":" + (other ? "true" : "false") + "}";
                }
                out += "]}";
            }
            out += "],\"overlaps\":[";
            for (std::size_t o = 0; o < mod.overlaps.size(); ++o) {
                const galaxy::mods::ModOverlap& overlap = mod.overlaps[o];
                out += std::string(o ? "," : "") + "{\"other\":" + json_text(overlap.other) + ",\"above\":" +
                       (overlap.other_above ? "true" : "false") + ",\"files\":" + std::to_string(overlap.files) +
                       ",\"textures\":" + std::to_string(overlap.textures) +
                       ",\"settings\":" + std::to_string(overlap.settings) + "}";
            }
            out += "]}";
        }
        out += "]}";
        std::printf("%s\n", out.c_str());
        return 0;
    }
    int failed = 0;
    std::string out = "{\"folder\":" + json_text(utf8_of(settings_folder / "mods")) + ",\"results\":[";
    // The same check as the game's own imports (validator.md 2 "at import"): against the packages placed now.
    const galaxy::mods::ImportCheck check = [](const fs::path& staged, const std::string& id, galaxy::mods::ImportResult& result) {
        std::vector<galaxy::modlog::StartPackage> others;
        for (const galaxy::mods::ModInfo& mod : galaxy::mods::scan_mods()) {
            if (mod.package && mod.id != id) others.push_back({mod.id, mod.path, mod.enabled});
        }
        const galaxy::modlog::ImportCheck found = galaxy::modlog::check_import(staged, others);
        result.checked = found.ran;
        result.refused = found.refused;
        for (const galaxy::modlog::ImportFinding& f : found.findings) {
            result.findings.push_back({f.error, f.code, f.file, f.text, f.line, f.column});
        }
        return !found.refused;
    };
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const galaxy::mods::ImportResult result = galaxy::mods::import_mod(sources[i], settings_folder / "mods", {}, nullptr, check);
        failed += result.ok ? 0 : 1;
        std::string findings;
        for (const galaxy::mods::ImportResult::Finding& f : result.findings) {
            findings += std::string(findings.empty() ? "" : ",") + "{\"code\":" + std::to_string(f.code) + ",\"error\":" +
                        (f.error ? "true" : "false") + ",\"file\":" + json_text(f.file) + ",\"text\":" + json_text(f.text) + "}";
        }
        out += std::string(i ? "," : "") + "{\"ok\":" + (result.ok ? "true" : "false") + ",\"id\":" + json_text(result.id) +
               ",\"replaced\":" + (result.replaced ? "true" : "false") +
               ",\"already\":" + (result.already ? "true" : "false") + ",\"title\":" + json_text(result.title) + ",\"files\":" + std::to_string(result.files) +
               ",\"bytes\":" + std::to_string(result.bytes) + ",\"kinds\":" + json_text(result.kinds) +
               ",\"message\":" + json_text(result.message) + ",\"checked\":" + (result.checked ? "true" : "false") +
               ",\"refused\":" + (result.refused ? "true" : "false") + ",\"findings\":[" + findings + "]}";
    }
    out += "],\"ok\":" + std::string(failed == 0 && !sources.empty() ? "true" : "false") + "}";
    std::printf("%s\n", out.c_str());
    return failed == 0 && !sources.empty() ? 0 : 1;
}

}  // namespace
#endif

namespace {

// The disc when --pak is not given: content/game.pak (as nebula-import and Setup write it) or game.pak, next to the
// program, in the current folder, or in the install root above it (versions/<version>/, the launcher's layout).
// Empty when none of them has one.
fs::path find_default_pak(const fs::path& program) {
    std::vector<fs::path> folders;
    std::error_code error;
    if (!program.empty()) folders.push_back(program);
    if (fs::path current = fs::current_path(error); !error) folders.push_back(std::move(current));
    if (program.parent_path().filename() == "versions") folders.push_back(program.parent_path().parent_path());
    for (const fs::path& folder : folders) {
        for (const fs::path& pak : {folder / "content" / "game.pak", folder / "game.pak"}) {
            if (fs::is_regular_file(pak, error)) return pak;
        }
    }
    return {};
}

// The saves when --nand is not given: a saves folder already next to the program or in the current folder (a test
// build run as docs/TEST_BUILDS.md says), else the launcher's (saves in Nebula's data folder, by the settings).
fs::path default_nand(const fs::path& program) {
    std::error_code error;
    if (!program.empty() && fs::is_directory(program / "saves", error)) return program / "saves";
    if (fs::is_directory("saves", error)) return fs::absolute("saves", error);
    if (const fs::path data = galaxy::nebula_data_directory(); !data.empty()) return data / "saves";
    return program.empty() ? fs::path("saves") : program / "saves";
}


// The command line's launch arguments (launch_args.h, docs/using-nebula/command-line-arguments.md), before anything reads them. Paths come
// from the UTF-16 command line on Windows: argv is in the system's code page, which cannot hold every folder name.
void read_launch_args(int argc, char** argv) {
    galaxy::LaunchArgs& args = galaxy::g_launch_args;
    bool imports_taken = false;  // --imports-taken 1
#if defined(_WIN32)
    int wide_count = 0;
    LPWSTR* const wide = CommandLineToArgvW(GetCommandLineW(), &wide_count);
#endif
    const auto path_at = [&](int index) {
#if defined(_WIN32)
        if (wide != nullptr && index < wide_count) {
            return fs::path(wide[index]);
        }
#endif
        return fs::path(argv[index]);
    };
    // A mods tool (--import-mod a.luma b.luma --pak P --data D) has its own words between the options, so only its
    // folder options are read, wherever they are (LumaLink's `run` names the data folder it found); a start's
    // arguments are read as the main loop reads them.
    if (argc >= 2 && is_mods_tool(argv[1])) {
        for (int i = 2; i + 1 < argc; ++i) {
            const std::string_view key = argv[i];
            if (key == "--data") {
                args.data = path_at(++i);
            } else if (key == "--settings") {
                args.settings = path_at(++i);
            } else if (key == "--crash-dumps") {
                args.crash_dumps = path_at(++i);
            } else if (key == "--shipped-mods") {
                args.shipped_mods = path_at(++i);
            }
        }
    }
    for (int i = 1; i + 1 < argc && !(argc >= 2 && is_mods_tool(argv[1])); ++i) {
        const std::string_view key = argv[i];
        // Each option takes the argument after it, but --host's port may be left out.
        if (std::strncmp(argv[i + 1], "--", 2) == 0) {
            continue;
        }
        if (key == "--data") {
            args.data = path_at(i + 1);
        } else if (key == "--settings") {
            args.settings = path_at(i + 1);
        } else if (key == "--crash-dumps") {
            args.crash_dumps = path_at(i + 1);
        } else if (key == "--gpu-validation") {
            args.gpu_validation = static_cast<unsigned>(std::strtoul(argv[i + 1], nullptr, 10));
        } else if (key == "--console-log") {
            args.console_log = argv[i + 1][0] == '1';
        } else if (key == "--gpu-start-retry") {
            args.gpu_start_retry = static_cast<unsigned>(std::strtoul(argv[i + 1], nullptr, 10));
        } else if (key == "--started-again") {
            args.started_again = static_cast<unsigned>(std::strtoul(argv[i + 1], nullptr, 10));
        } else if (key == "--start-again-of") {
            args.start_again_of = argv[i + 1];
        } else if (key == "--hidden") {
            args.started_hidden = argv[i + 1][0] == '1';  // Windows reads its start's window instead, below
        } else if (key == "--renderer") {
            const std::string_view api = argv[i + 1];
            args.renderer = api == "d3d12" ? 1u : api == "d3d11" ? 2u : api == "vulkan" ? 3u : 0u;
        } else if (key == "--timeline") {
            args.timeline = path_at(i + 1);
        } else if (key == "--extension") {
            args.extensions.push_back(path_at(i + 1));
        } else if (key == "--import") {
            args.imports.push_back(path_at(i + 1));
        } else if (key == "--imports-taken") {
            imports_taken = argv[i + 1][0] == '1';
        } else if (key == "--safe-start") {
            args.safe_start = argv[i + 1][0] == '1';
        } else if (key == "--shipped-mods") {
            args.shipped_mods = path_at(i + 1);
        } else if (key == "--gpu-driver") {
            args.gpu_driver = path_at(i + 1);
        } else if (key == "--gpu-driver-library") {
            args.gpu_driver_library = argv[i + 1];
        } else if (key == "--gpu-driver-hooks") {
            args.gpu_driver_hooks = path_at(i + 1);
        } else if (key == "--mod-dev") {
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
            args.mod_dev.emplace_back(argv[i + 1]);  // desktop only: no dev mode on a phone or tablet
#endif
        } else if (key == "--seconds") {
            args.timed = std::atof(argv[i + 1]) > 0.0;
        } else if (key == "--diagnostics") {
            std::string_view unknown;
            unsigned what = 0u;
            if (!galaxy::diagnostics_from_text(argv[i + 1], what, unknown)) {
                die("--diagnostics: no diagnostic is named %.*s (docs/using-nebula/diagnostics.md)", static_cast<int>(unknown.size()), unknown.data());
            }
            args.diagnostics |= what;
        } else {
            continue;
        }
        ++i;
    }
    // A restart of a start whose window took its --import files (they were imported then).
    if (imports_taken) args.imports.clear();
#if defined(_WIN32)
    if (wide != nullptr) {
        LocalFree(wide);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    GetStartupInfoW(&startup);
    args.started_hidden = (startup.dwFlags & STARTF_USESHOWWINDOW) != 0u && startup.wShowWindow == SW_HIDE;
#endif
}

}  // namespace

#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
// The Windows hosts are window programs (CMakeLists.txt nebula_host_cpu_entry): a start from Explorer, a shortcut or
// the starter has no console, and the log goes to logs/latest.log. Output a caller captures (a pipe, a file) stays as
// given. Otherwise --console-log 1 and the report modes write to the console of the terminal that started them; with
// `own` and no such terminal, to a console of their own.
void use_console(bool own) {
    const DWORD output = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    if (output == FILE_TYPE_PIPE || output == FILE_TYPE_DISK || output == FILE_TYPE_CHAR) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !(own && AllocConsole())) return;
    FILE* stream = nullptr;
    if (_wfreopen_s(&stream, L"CONOUT$", L"w", stdout) == 0) {
        SetStdHandle(STD_OUTPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout))));
    }
    if (_wfreopen_s(&stream, L"CONOUT$", L"w", stderr) == 0) {
        SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));
    }
}
#endif

int main(int argc, char** argv) {
#if !defined(_WIN32)
    gArgv = argv;
    galaxy::luma_guard::set_crash_line(crash_line_from_signal);
#endif
#if defined(_WIN32)
    // The crash watcher's own run (crash_watch.h): before anything of the game's.
    if (argc >= 4 && std::strcmp(argv[1], galaxy::crash_watch::kSwitch) == 0) {
        const int code = galaxy::crash_watch::run(argc, argv);
#if defined(NEBULA_HOST_RENDERER)
        // The game has ended (0): the Windows HDR it turned on and did not turn off goes off (render/os_hdr.h). The list's
        // path is argv[4], read wide.
        int count = 0;
        LPWSTR* wide = code == 0 ? CommandLineToArgvW(GetCommandLineW(), &count) : nullptr;
        if (wide != nullptr) {
            if (count >= 5) galaxy::render::os_hdr::restore_listed(wide[4], std::wcstoul(wide[2], nullptr, 10));
            LocalFree(wide);
        }
#endif
        return code;
    }
#endif
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
    // The renderer check's own run (renderer_check.h): which renderers each card runs, written out, nothing else.
    if (argc == 2 && std::strcmp(argv[1], galaxy::render::kRendererCheckSwitch) == 0) {
        return galaxy::render::run_renderer_check();
    }
#endif
#if defined(NEBULA_HOST_APPLE)
    // An app has no command line: the app's own layout names the game, the saves and the module.
    galaxy::apple::launch_arguments(argc, argv);
#endif
#if defined(NEBULA_HOST_NATIVE_APPLE)
    // The first start: the player's disc image becomes the game's data, or the player leaves.
    if (!galaxy::apple::ensure_game_data(argc, argv)) {
        return 0;
    }
#endif
    read_launch_args(argc, argv);
    galaxy::mods::set_dev_mods(::galaxy::launch_args().mod_dev);
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
#if defined(NEBULA_DIAG_REPORTS)
    const bool report_mode = argc >= 2 && galaxy::diag::is_command_mode(argv[1]);
#else
    const bool report_mode = false;
#endif
    if (::galaxy::launch_args().console_log || report_mode) {
        use_console(::galaxy::launch_args().console_log);
    }
#endif
#if defined(NEBULA_DIAG_REPORTS)
    // The diagnostics report modes (--send-report and the rest): files and one HTTPS POST, nothing else, then exit.
    if (const int code = galaxy::diag::run_command_line(argc, argv); code >= 0) {
        return code;
    }
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    // A tool's hidden start keeps off the player's data (hidden_start.h), before anything is written.
    {
        int count = 0;
        LPWSTR* const wide = CommandLineToArgvW(GetCommandLineW(), &count);
        const char* const error =
            wide != nullptr ? galaxy::host::hidden_start_error(::galaxy::launch_args().started_hidden, count, wide)
                            : (::galaxy::launch_args().started_hidden ? "cannot read the command line" : nullptr);
        if (wide != nullptr) {
            LocalFree(wide);
        }
        if (error != nullptr) {
            std::fprintf(stderr, "[hidden-start] %s\n", error);
            return 2;
        }
    }
#elif defined(NEBULA_HOST_SDL) && !defined(__ANDROID__) && !defined(NEBULA_HOST_APPLE)
    // The same on Linux and macOS, over main's own arguments.
    if (const char* const error = galaxy::host::hidden_start_error(::galaxy::launch_args().started_hidden, argc, argv)) {
        std::fprintf(stderr, "[hidden-start] %s\n", error);
        return 2;
    }
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    if (argc >= 3 && std::strcmp(argv[1], "--crash-report") == 0) {
        return show_crash_report_dialog();
    }
    // Asked for another renderer (--renderer, else the settings' choice): its program runs in this one's place before
    // this one takes anything (hand_off_to).
#if defined(_M_X64) && defined(NEBULA_HOST_D3D12)
    {
        using galaxy::render::RendererKind;
        const unsigned given = ::galaxy::launch_args().renderer;
        const RendererKind wanted = given != 0u ? static_cast<RendererKind>(given) : galaxy::render::renderer_in_settings_file();
        if (wanted == RendererKind::kD3D11 || wanted == RendererKind::kVulkan) {
            hand_off_to(wanted, given != 0u ? "--renderer" : "the settings' renderer");
        }
    }
#endif
    if (claim_game_instance(argc, argv)) {
        return 0;
    }
    // Displays a restart handed over with Windows HDR on, or that an end the watcher missed left on (render/os_hdr.h).
    galaxy::render::os_hdr::adopt();
#endif
#if defined(NEBULA_HOST_SDL)
    if (!galaxy::sdl::start()) {
        return 1;
    }
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    // A player's start has nowhere to print (a shortcut with no terminal): the log goes to logs/latest.log beside the
    // settings, the last start's to previous.log. A test or a shell capturing the output to a pipe or a file keeps it.
    {
        const DWORD output = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
        if (output != FILE_TYPE_PIPE && output != FILE_TYPE_DISK && !::galaxy::launch_args().console_log) {
            const fs::path settings = galaxy::runtime_settings_folder();
            std::error_code error;
            if (!settings.empty() && fs::create_directories(settings / "logs", error) | fs::is_directory(settings / "logs", error)) {
                fs::rename(settings / "logs" / "latest.log", settings / "logs" / "previous.log", error);
                FILE* log = nullptr;
                if (_wfreopen_s(&log, (settings / "logs" / "latest.log").c_str(), L"w", stdout) == 0) {
                    // A window program's error stream has no file to point elsewhere until it is given one.
                    FILE* error = nullptr;
                    if (_fileno(stderr) < 0) (void)_wfreopen_s(&error, L"NUL", L"w", stderr);
                    if (_fileno(stderr) >= 0) _dup2(_fileno(stdout), _fileno(stderr));
                }
            }
        }
    }
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    // --diagnostics timeline: the frame pipeline's events (frame_timeline.h), written at exit.
    galaxy::timeline::initialize();
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    // Physical-pixel DPI awareness before any window (sizes and mouse coordinates in pixels), and CRT assertion and
    // error reports to stderr instead of dialogs.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | SEM_NOGPFAULTERRORBOX);
#endif
#if defined(_WIN32)
    install_crash_reporter();
#endif
#if defined(NEBULA_HOST_RENDERER)
    if (argc >= 2 && is_mods_tool(argv[1])) {
        return run_mods_tool();
    }
#endif
    if (::galaxy::launch_args().started_hidden) {
        log_line("host:", "started hidden");
    }
    fs::path pak, saves, module;
    // A path in the command line: argv is in the system's code page, which cannot hold every folder name.
    const auto arg_path = [&](int index) {
#if defined(_WIN32)
        int count = 0;
        if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
            fs::path path = index < count ? fs::path(wide[index]) : fs::path(argv[index]);
            LocalFree(wide);
            return path;
        }
#endif
        return fs::path(argv[index]);
    };
    // The module beside the program for this CPU (default_module_path: x86-64-v2\ on a CPU without x86-64-v3).
    const auto default_module = [&] {
#if defined(_WIN32)
        return default_module_path(arg_path(0).parent_path());
#else
        return default_module_path(fs::path(argv[0]).parent_path());
#endif
    };
    // --luma-describe <file> [--module <path>]: the module's type rows as JSON (docs/modding/reference/lumascript/the-game-by-name.md),
    // compared by the build with the generator's (tools/lumacore/compare_types.py). --luma-services <file> [--module
    // <path>]: the services catalog as JSON (luma_service_provider.h luma_services_describe), the names folder's
    // luma_services.json. No disc; the module is attached with its heap, log and fatal errors (to stderr) only, as its
    // malloc is the host's and its constructors run at attach.
    const bool describe_services = argc >= 2 && std::strcmp(argv[1], "--luma-services") == 0;
    if (argc >= 2 && (describe_services || std::strcmp(argv[1], "--luma-describe") == 0)) {
        // Exactly `<file>` or `<file> --module <path>`: anything else is said, never ignored (A-LD1).
        if (!(argc == 3 || (argc == 5 && std::strcmp(argv[3], "--module") == 0))) {
            die("usage: %s <file> [--module <path>] (docs/using-nebula/command-line-arguments.md)", argv[1]);
        }
        const fs::path out = arg_path(2);
        const fs::path from = argc == 5 ? arg_path(4) : default_module();
        load_module(from);
        static NebulaHostServices bare{};
        bare.version = NEBULA_GAME_API_VERSION;
        bare.size = sizeof(bare);
        bare.log = [](void*, const char* text) { std::fprintf(stderr, "%s\n", text); };
        bare.fatal = [](void*, const char* text) {
            std::fprintf(stderr, "luma-describe: the game module stopped: %s\n", text);
            std::fflush(stderr);
            std::_Exit(3);
        };
        bare.heap_alloc = [](void*, size_t size) -> void* { return std::malloc(size); };
        bare.heap_realloc = [](void*, void* data, size_t size) -> void* { return std::realloc(data, size); };
        bare.heap_free = [](void*, void* data) { std::free(data); };
        if (gModule.attach(&bare) != NEBULA_GAME_API_VERSION) {
            die("the game module's API version differs from the host's (%u)", NEBULA_GAME_API_VERSION);
        }
        // Both exports take the same writer: (user, text, length), the pieces in order.
        using DescribeFn = void (*)(void (*)(void*, const char*, uint32_t), void*);
        const auto describe = reinterpret_cast<DescribeFn>(
            find_symbol(gModule.module, describe_services ? "luma_services_describe" : "nebula_game_luma_describe"));
        std::ofstream file(out, std::ios::binary | std::ios::trunc);
        describe([](void* user, const char* text, uint32_t length) { static_cast<std::ofstream*>(user)->write(text, length); },
                 &file);
        file.close();
        if (!file) {
            std::fprintf(stderr, "%s: %s could not be written\n", argv[1] + 2, utf8_of(out).c_str());
            return 2;
        }
        return 0;
    }
    attach_extensions();
    boot_mark("main");
    double seconds = 0;
    std::error_code error;
    [[maybe_unused]] bool benchmark = false;  // --benchmark 1: benchmark.h's run, then exit
    fs::path benchmark_out;
    [[maybe_unused]] int home = 0;  // --home 1: the home screen at the title's hold
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        // Each option takes the argument after it, but --host's port may be left out.
        const char* value = i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0 ? argv[i + 1] : nullptr;
        if (value != nullptr) {
            ++i;
        }
#if defined(NEBULA_HOST_RENDERER)
        if (key == "--host" && value == nullptr) {
            gCoopAutoStart = CoopAutoStart::Host;
            gCoopAutoPort = galaxy::kCoopDefaultPort;
            continue;
        }
#endif
        if (value == nullptr) {
            die("%s takes a value", key.c_str());
        }
        if (key == "--pak") {
            pak = arg_path(i);
        } else if (key == "--saves") {
            saves = arg_path(i);
        } else if (key == "--module") {
            module = arg_path(i);
        } else if (key == "--diagnostics" || key == "--timeline" || key == "--data" || key == "--settings" ||
                   key == "--crash-dumps" || key == "--gpu-validation" || key == "--console-log" ||
                   key == "--gpu-start-retry" || key == "--extension" || key == "--import" || key == "--imports-taken" ||
                   key == "--safe-start" || key == "--shipped-mods" || key == "--mod-dev" || key == "--renderer" || key == "--started-again" ||
                   key == "--start-again-of" || key == "--hidden" || key == "--gpu-driver" || key == "--gpu-driver-library" ||
                   key == "--gpu-driver-hooks") {
            // Read above, before anything uses them.
        } else if (key == "--seconds") {
            seconds = std::atof(value);
#if defined(NEBULA_HOST_RENDERER)
        } else if (key == "--host") {
            // Online co-op (docs/COOP_PLAN.md): as the main menu opens, Host plays the last played save on this port.
            std::string ignored;
            std::uint16_t port = 0u;
            if (!galaxy::parse_coop_endpoint(std::string("host:") + value, ignored, port) ||
                std::string(value).find_first_not_of("0123456789") != std::string::npos) {
                die("--host takes a port, 1 to 65535");
            }
            gCoopAutoStart = CoopAutoStart::Host;
            gCoopAutoPort = port;
        } else if (key == "--join") {
            // As the main menu opens, Join finds the room code on NebulaNet or connects to the address.
            std::string address;
            std::uint16_t port = 0u;
            if (!coop_join_code(value) && !galaxy::parse_coop_endpoint(value, address, port)) {
                die("--join takes a room code, an address, address:port or [IPv6]:port");
            }
            gCoopAutoStart = CoopAutoStart::Join;
            gCoopAutoAddress = value;
#endif
        } else if (key == "--home") {
            home = std::atoi(value) != 0 ? 1 : 0;
        } else if (key == "--benchmark") {
            benchmark = std::atoi(value) != 0;
        } else if (key == "--benchmark-out") {
            benchmark_out = arg_path(i);
        } else {
            die("unknown option %s", key.c_str());
        }
    }
    // Without --pak and --saves, the disc and saves are found as find_default_pak and default_nand say, so the
    // program starts with no arguments from a folder that holds content/game.pak.
    fs::path program = galaxy::program_directory();
    if (program.empty()) {
        program = fs::absolute(arg_path(0), error).parent_path();
    }
    if (pak.empty()) {
        pak = find_default_pak(program);
        if (pak.empty()) {
            die("no game.pak: pack your disc with nebula-import into a content folder next to the program, or name it: "
                "--pak <game.pak> [--saves <saves folder>] [--module <smg_game.dll>] [--seconds N]");
        }
    }
#if defined(NEBULA_HOST_RENDERER)
    if (benchmark) {
        // No save folder: the game's saves stay in memory. Straight to the title, no time limit (it quits when done).
        saves.clear();
        home = 0;
        seconds = 0;
        galaxy::bench::Options options;
        options.pak_folder = pak.parent_path();
        options.out_file = benchmark_out;
        (void)galaxy::bench::start(options);
    }
    gPakFolder = pak.parent_path();
#endif
    if (saves.empty() && !benchmark) {
        saves = default_nand(program);
    }
    if (!saves.empty()) saves = game_instance_saves(saves);
    if (const unsigned instance = galaxy::game_instance(); instance > 1u) {
        log_line("host:", ("game " + std::to_string(instance) + ", with its own settings and saves in " +
                           utf8_of(galaxy::game_instance_folder())).c_str());
    }
    log_line("paths:", ("disc " + pak.string() + ", saves " + saves.string()).c_str());
    if (module.empty()) {
        module = default_module_path(program);
    }
    gSaveRoot = saves;
    gCoopSaves.emplace(gSaveRoot / "coop_saves");
    gCoopMenu.set_saves(&*gCoopSaves);
    gCoopMenu.set_mods_source(coop_room_mods);
    if (!open_pak(pak)) return 2;
    {
        // The room compares games by id, revision and the store's file table (its entries, as the import laid them
        // out): boot.bin's first bytes and no main.dol, which the cooked store does not keep.
        uint8_t boot[8] = {};
        std::memcpy(boot, gPak.id, 6u);
        boot[7] = static_cast<uint8_t>(gPak.store.revision());
        std::vector<uint8_t> table;
        for (int32_t i = 0;; ++i) {
            const std::optional<galaxy::assets::Entry> entry = gPak.store.entry(i);
            if (!entry) break;
            const uint64_t fields[3] = {entry->folder ? 1u : 0u, entry->length, entry->position};
            const auto* bytes = reinterpret_cast<const uint8_t*>(fields);
            table.insert(table.end(), bytes, bytes + sizeof(fields));
            if (entry->name != nullptr) table.insert(table.end(), entry->name, entry->name + std::strlen(entry->name) + 1u);
        }
        gCoopDisc = galaxy::coop::describe_disc(boot, {}, table);
    }
    galaxy::luma_assets::set_store(&gPak.store);  // the assets service reads it (services/assets.md)
    galaxy::luma_audio::set_store(&gPak.store);   // and the audio service a mod's cooked sounds (services/audio.md)
    galaxy::modlog::set_check_store(&gPak.store);  // and the validator, at the start's check and at import
    boot_mark("disc opened");
    {
        // Two modules (game_module.h): the plain one unless a mod's code needs the rows. --module names the
        // rows module; the plain one is beside it. Updates imported while a mod was in use land first, as below.
        // A headless host (no NEBULA_HOST_RENDERER) is the test host: no settings folder, no player's mods, and the
        // harness's extensions hook and call by name, so it keeps the module --module names (the rows one).
#if defined(_WIN32)
        wait_for_hand_over();  // a start-again: nothing reads or writes the mods folder before the old one lets go of it
#endif
#if defined(NEBULA_HOST_RENDERER)
        fs::path mods_folder;
        const fs::path settings_folder = galaxy::runtime_settings_folder();
        if (!settings_folder.empty()) {
            fs::create_directories(settings_folder / "mods", error);
            galaxy::mods::apply_pending_updates(settings_folder / "mods");
            mods_folder = settings_folder / "mods";
        }
        // The safe start is taken before the choice (and read again at the mods below): with every mod off no mod's
        // code starts, so the plain module loads; the harness's own game half stays on and keeps the rows.
        bool safe = false;
        if (!mods_folder.empty()) {
            galaxy::mods::set_mods_folder(mods_folder, settings_folder / "mods.ini", std::string(gPak.store.game_id()));
            // The mods that come with the game (LumaKit), installed or brought up to date before anything reads the
            // mods. A library among them loads only while a mod that is on needs it.
            fs::path shipped = ::galaxy::launch_args().shipped_mods;
            if (shipped.empty() && !galaxy::package_directory().empty()) shipped = galaxy::package_directory() / "luma" / "mods";
            for (const galaxy::mods::ShippedMod& mod : galaxy::mods::install_shipped_mods(shipped, mods_folder)) {
                const std::string said = mod.ok ? mod.id + " " + mod.version + " installed from " + mod.file
                                                : mod.file + " was not installed: " + mod.message;
                log_line("mods:", said.c_str());
            }
            safe = galaxy::mods::take_safe_start(safe_start_asked()) != galaxy::mods::SafeStart::kNo;
        }
        std::string why;
        const fs::path rows_module = module;
        module = galaxy::game_module::choose(rows_module, mods_folder, settings_folder / "mods.ini", std::string(gPak.store.game_id()),
                                             false, safe, is_harness_half("luma-test") ? "luma-test" : "", &why);
        galaxy::game_module::set_plain_loaded(module != rows_module);
        log_line("game module:", why.c_str());
#else
        galaxy::game_module::set_plain_loaded(false);
        log_line("game module:", "rows (headless test host)");
#endif
        boot_mark("game module chosen");
    }
    load_module(module);
    boot_mark("game module loaded");
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::set_texture_pack_game_id(gPak.id);  // packs' and dumps' game folders: the disc's release
    if (!galaxy::bench::running()) {  // a benchmark's game has no Miis: their library is in the save folder
        // Mii import: the files in the import folder (next to the settings: any Mii files, mii_formats.h)
        // join the library before the game reads it; the settings import more while it runs.
        const fs::path settings_folder = galaxy::runtime_settings_folder();
        const fs::path import_folder = settings_folder.empty() ? fs::path{} : settings_folder / "miis";
        if (!import_folder.empty()) {
            fs::create_directories(import_folder, error);
        }
        galaxy::host::set_mii_library(mii_library_path(), console_mii_database_path(), import_folder,
                                      [](galaxy::host::MiiRecord& mii) {
                                          galaxy::host::fix_wii_mii(mii);
                                          const NebulaMii fields = nebula_mii(galaxy::host::decode_wii_mii(mii));
                                          return gModule.mii_valid(&fields) != 0u;
                                      });
        std::string message;
        (void)galaxy::host::import_mii_folder(&message);
        std::fprintf(stderr, "[mii] import folder %s: %s (%u in the library)\n", utf8_of(import_folder.filename()).c_str(),
                     message.c_str(), galaxy::host::mii_library_count());
        (void)galaxy::host::take_mii_installs();  // in the file the game reads at boot
    }
#if defined(NEBULA_HOST_RENDERER)
    // The player's settings (in --settings' folder when given) before the mods: the language they choose
    // picks the mods' text (and is the game's language from then on).
    try {
        galaxy::load_runtime_settings_from_file();
    } catch (const std::exception& failure) {
        die("the settings did not load: %s", failure.what());
    }
#endif
    {
        // Mods: the mods folder next to the settings (galaxy::mods), applied before the game reads its disc.
        const fs::path settings_folder = galaxy::runtime_settings_folder();
        const std::string game_id(gPak.store.game_id());
        const galaxy::saves::SaveFolders save_folders = galaxy::saves::save_folders(std::string_view(gPak.id, 4));
        gOldSaveDataPath = save_folders.earlier;
        gSaveDataPath = save_folders.current;
        if (!gSaveRoot.empty()) fs::create_directories(gSaveRoot / gSaveDataPath.substr(1), error);  // the release's own save folder
        if (!gSaveRoot.empty()) {
            galaxy::save_transfer::set_game_saves(gSaveRoot, std::string_view(gPak.id, 4));  // SAVES rows
            // The import file by file: the game module reads the picked save (nebula_save_import.h).
            galaxy::save_transfer::set_game_reader(
                reinterpret_cast<NebulaSaveImportRead>(find_symbol(gModule.module, "nebula_save_import_read")));
        }
        galaxy::luma_mods::set_save_half({kept_save_ids, kept_save_bytes, forget_save});
        if (!settings_folder.empty()) {
            // A second game (galaxy/game_instance.h) plays the first's installed mods, with its own list of which are on.
            const fs::path& instance = galaxy::game_instance_folder();
            const fs::path mods_home = instance.empty() ? settings_folder : instance.parent_path().parent_path();
            fs::create_directories(mods_home / "mods", error);
            // Updates imported while their mod's folder was in use take its place before anything reads the mods.
            galaxy::mods::apply_pending_updates(mods_home / "mods");
            galaxy::mods::set_mods_folder(mods_home / "mods", settings_folder / "mods.ini", game_id);
            // NebulaCoop's built-in mods (Character Movesets): always there, as their latest, off until turned on.
            (void)galaxy::mods::install_builtin_mods();
            // A join Nebula started again for: this start plays the room's mods (prepare_mod_files), then joins.
            if (std::optional<galaxy::coop::NextJoin> join = galaxy::coop::take_next_join(coop_next_join_file())) {
                std::vector<galaxy::mods::RoomModChoice> room;
                for (const galaxy::coop::SharedMod& mod : join->mods) {
                    galaxy::mods::RoomModChoice choice{mod.id, mod.folder, mod.own_text, {}};
                    for (const auto& [key, selected] : mod.options) choice.options[key] = selected;
                    room.push_back(std::move(choice));
                }
                galaxy::mods::set_room_mods(std::move(room));
                gCoopAutoStart = CoopAutoStart::Join;
                gCoopAutoAddress = join->address;
                std::fprintf(stderr, "[coop] starting with the room's mods (%s) to join %s\n",
                             galaxy::coop::mods_text(join->mods).c_str(), join->address.c_str());
                gCoopStartedRoomMods = std::move(join->mods);
            }
            galaxy::mods::set_mods_store_file(gPak.store_file);
            // One scan for the start, in the language played (a .luma's layers and text follow it): the mods
            // mods.ini has on give their packs and movies from the first frame, and the same list is planned. The
            // language played is one the disc or a language pack on has (a Japanese disc plays Japanese unless a
            // pack brings the chosen one): the disc's are known first, a pack's after the scan finds it.
            std::vector<std::string> game_languages = gPak.store.language_folders();
#if defined(NEBULA_HOST_RENDERER)
            galaxy::set_game_languages(game_languages);
#endif
            const auto played = [] {
                return static_cast<unsigned>(galaxy::game_language_resolved(galaxy::get_runtime_settings().language));
            };
            const unsigned scanned_in = played();
            galaxy::mods::set_played_language(scanned_in);
            std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods(game_id);
            // Safe start: every mod off for this start only (the copy planned; mods.ini stays the player's).
            if (const galaxy::mods::SafeStart safe = galaxy::mods::take_safe_start(safe_start_asked());
                safe != galaxy::mods::SafeStart::kNo) {
                log_line("mods:", safe == galaxy::mods::SafeStart::kAsked
                                      ? "safe start: every mod is off for this start"
                                      : "safe start: the last run crashed with mods on; every mod is off for this start");
                // The harness's own game half stays on (is_harness_half).
                for (galaxy::mods::ModInfo& mod : mods) {
                    if (!is_harness_half(mod.id)) mod.enabled = false;
                }
            } else if (std::any_of(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& mod) { return mod.enabled; })) {
                // A crash from here on (the cook, the packs, the game) makes the next start safe; svc_mods_start
                // narrows it to the code when code is on.
                gCodeCrashFile = galaxy::mods::safe_start_file();
            }
            bool added_language = false;
            for (const galaxy::mods::ModInfo& mod : mods) {
                if (!mod.enabled) continue;
                for (const std::string& language : mod.languages) {
                    if (std::find(game_languages.begin(), game_languages.end(), language) == game_languages.end()) {
                        game_languages.push_back(language);
                        added_language = true;
                    }
                }
            }
#if defined(NEBULA_HOST_RENDERER)
            if (added_language) {
                galaxy::set_game_languages(game_languages);
                if (played() != scanned_in) {
                    galaxy::mods::set_played_language(played());
                    mods = galaxy::mods::scan_mods(game_id);
                }
            }
#else
            (void)added_language;
#endif
            gGameLanguages = game_languages;
            galaxy::render::set_texture_pack_mod_folders(galaxy::mods::texture_folders(mods),
                                                         galaxy::mods::texture_folder_categories_off(mods));
            galaxy::render::set_movie_mod_folders(galaxy::mods::movie_folders(mods));
            prepare_mod_files(settings_folder, game_id, std::move(mods));
        }
#if defined(NEBULA_HOST_RENDERER)
        // Ray tracing's tables, with the mods' rows (their merge/NebulaData/RtStages.tsv and RtEffects.tsv) now that
        // their store is attached.
        load_rt_tables();
#endif
        {
            // The session's mods, for crash reports: one line each, and whether one changes the game itself.
            std::string lines;
            bool change_game = false;
            for (const galaxy::mods::ModInfo& mod : galaxy::mods::session_mods().started_with) {
                if (!mod.enabled) continue;
                std::string kinds;
                for (const auto& [on, name] : {std::pair{mod.riivolution, "Riivolution"},
                                               std::pair{mod.files || mod.loose, "game files"},
                                               std::pair{mod.textures, "textures"}, std::pair{mod.movies, "movies"}}) {
                    if (on) kinds += kinds.empty() ? name : std::string(", ") + name;
                }
                lines += "  " + mod.id + (kinds.empty() ? "" : " (" + kinds + ")") + "\n";
                change_game = change_game || mod.riivolution || mod.files || mod.loose;
            }
            galaxy::crash::set_session_mods(lines, change_game);
        }
        {
            // The game's version and its languages (the disc's and the language packs' on), which the language
            // setting offers.
            const std::vector<std::string> languages = gGameLanguages.empty() ? gPak.store.language_folders() : gGameLanguages;
            std::string list;
            for (const std::string& language : languages) list += (list.empty() ? "" : ", ") + language;
            const std::string text = game_id + " revision " + std::to_string(gPak.store.revision()) + ": " +
                                     (list.empty() ? std::string("no language folders") : list);
            log_line("disc:", text.c_str());
#if defined(NEBULA_HOST_RENDERER)
            galaxy::set_game_languages(languages);
#endif
        }
    }
#endif

    boot_mark("settings and mods");
    static NebulaHostServices services{};
    services.version = NEBULA_GAME_API_VERSION;
    services.size = sizeof(services);
    services.log = svc_log;
    services.fatal = svc_fatal;
    services.heap_alloc = [](void*, size_t size) -> void* { return std::malloc(size); };
    services.heap_realloc = [](void*, void* data, size_t size) -> void* { return std::realloc(data, size); };
    services.heap_free = [](void*, void* data) { std::free(data); };
    services.movie_frame = svc_movie_frame;
    services.presence = svc_presence;
    services.achievement = svc_achievement;
    services.achievement_date = svc_achievement_date;
    services.mod_achievement_count = svc_mod_achievement_count;
    services.mod_achievement = svc_mod_achievement;
    services.mod_achievement_unlocked = svc_mod_achievement_unlocked;
    services.stat_add = svc_stat_add;
    services.stat_get = svc_stat_get;
    services.settings_view = svc_settings_view;
    services.benchmark_state = svc_benchmark_state;
    services.settings_input = svc_settings_input;
    services.sharp_2d = svc_sharp_2d;
    services.button_icons = svc_button_icons;
    services.layer_2d_lines = svc_layer_2d_lines;
    services.pointer_wheel = svc_pointer_wheel;
    services.galaxy_select_stage = svc_galaxy_select_stage;
    services.galaxy_select_names_wanted = svc_galaxy_select_names_wanted;
    services.galaxy_select_name = svc_galaxy_select_name;
    services.take_pause_request = svc_take_pause_request;
    services.post_register = svc_post_register;
    services.post_free = svc_post_free;
    // A mod's request screen pauses the game as Start does (services/mods.md 9, services/save.md 8).
    galaxy::mods::set_screen_pause([] { gPauseRequest.store(true, std::memory_order_release); });
    services.test_frame_step = deterministic_mode() ? svc_test_frame_step : nullptr;
    services.texture_hd = svc_texture_hd;
    services.model_register = svc_model_register;
    services.model_free = svc_model_free;
    services.mesh_register = svc_mesh_register;
    services.mesh_free = svc_mesh_free;
    services.texture_register = svc_texture_register;
    services.texture_update = svc_texture_update;
    services.texture_free = svc_texture_free;
    services.texture_target = svc_texture_target;
    services.texture_table = svc_texture_table;
    services.material_register = svc_material_register;
    services.pipeline_register = svc_pipeline_register;
    services.submit_frame = svc_submit_frame;
    services.probe_results = svc_probe_results;
    services.mii_count = svc_mii_count;
    services.mii_record = svc_mii_record;
    services.clock_ns = svc_clock_ns;
    services.calendar_ns = svc_calendar_ns;
    services.asset_find = svc_asset_find;
    services.asset_find_game = svc_asset_find_game;
    services.asset_entry = svc_asset_entry;
    services.asset_child = svc_asset_child;
    services.asset_added_child = svc_asset_added_child;
    services.asset_read = svc_asset_read;
    services.memory_reserve = svc_memory_reserve;
    services.memory_commit = svc_memory_commit;
    services.memory_decommit = svc_memory_decommit;
    services.memory_release = svc_memory_release;
    services.asset_map = svc_asset_map;
    services.asset_map_private = svc_asset_map_private;
    services.asset_unmap = svc_asset_unmap;
    services.game_id = svc_game_id;
    services.frame_wait = svc_frame_wait;
    services.frame_begun = svc_frame_begun;
    services.save_request = svc_save_request;
    services.system_setting = svc_system_setting;
    services.input_read = svc_input_read;
    services.input_control = svc_input_control;
    services.speaker_samples = svc_speaker_samples;
    services.input_players = svc_input_players;
    services.voices_setup = svc_audio_setup;
    services.voices_mix = svc_audio_mix;
    services.voices_frame = svc_audio_frame;
    services.movie_audio = svc_movie_audio;
    services.movie_audio_position = svc_movie_audio_position;
    services.movie_audio_control = svc_movie_audio_control;
    services.sound_clock = svc_audio_control;
    services.home_menu = svc_home_menu;
    services.game_context = svc_game_context;
    services.app_exit = svc_app_exit;
    services.app_restart = svc_app_restart;
    services.mods_start = svc_mods_start;
    services.galaxy_travel = svc_galaxy_travel;
    services.display_extent = svc_display_extent;
    services.option = svc_option;
    services.game_stage = svc_game_stage;
    services.coop_player = svc_coop_player;
    services.coop_menu = svc_coop_menu;
    services.coop_chat = svc_coop_chat;
    gCoopChat.attach(gCoopSession);
    galaxy::coop::add_room_commands(gCoopChat, gCoopSession);
    add_coop_cheat_commands();
    services.coop_list = svc_coop_list;
    services.coop_remote = svc_coop_remote;
    services.coop_world_report = svc_coop_world_report;
    services.coop_world_take = svc_coop_world_take;
    services.coop_star_count = svc_coop_star_count;
    services.coop_rules = svc_coop_rules;
    services.coop_achievement_take = svc_coop_achievement_take;
    services.coop_popup_take = svc_coop_popup_take;
    services.coop_character_wanted = svc_coop_character_wanted;
    services.coop_command_add = svc_coop_command_add;
    services.coop_command_take = svc_coop_command_take;
    services.coop_chat_say = svc_coop_chat_say;
    services.coop_palette_wanted = svc_coop_palette_wanted;
    services.coop_objects_report = svc_coop_objects_report;
    services.coop_objects_take = svc_coop_objects_take;
#if defined(NEBULA_HOST_RENDERER)
    // The display configuration (in --settings' folder when given), loaded before the window; the settings overlay
    // reads its frame art from the game files.
    try {
        galaxy::render::load_render_config_from_file();
    } catch (const std::exception& failure) {
        die("the settings did not load: %s", failure.what());
    }
    // Discord: never from a hidden start (a test), which must leave the player's status alone.
    if (!::galaxy::launch_args().started_hidden) {
        galaxy::discord::set_enabled_provider([] { return galaxy::get_runtime_settings().discord_status; });
#if defined(_WIN32)
        // A Join pressed in Discord while Nebula is closed starts it: through the install root's Nebula.exe when this
        // runs from an install's <root>\versions\<version> (the same program the Start menu opens, with the install's
        // disc and saves), else as this start was, with its program, disc and saves. The first game's only; the others
        // are started from it.
        if (galaxy::game_instance() == 1u) {
            const auto quoted = [](const fs::path& path) { return L"\"" + path.wstring() + L"\""; };
            const fs::path program(::galaxy::windows::executable_path());
            const fs::path versions = program.parent_path().parent_path();
            const fs::path start = versions.parent_path() / L"Nebula.exe";
            galaxy::discord::set_launch_command(
                _wcsicmp(versions.filename().c_str(), L"versions") == 0 && fs::is_regular_file(start, error)
                    ? quoted(start)
                    : quoted(program) + L" --pak " + quoted(fs::absolute(pak, error)) + L" --saves " +
                          quoted(fs::absolute(saves, error)));
        }
#endif
        galaxy::discord::start(kDiscordClientId);
    }
    const galaxy::render::RenderConfig render_config = galaxy::render::get_render_config();
    if (!galaxy::render::initialize(static_cast<int>(render_config.window_width),
                                    static_cast<int>(render_config.window_height))) {
        const std::string reason = galaxy::render::startup_failure();
        if (retry_start_for_gpu_memory(reason)) {
            finish_and_exit(true);
        }
#if defined(_WIN32) && defined(_M_X64)
        // A renderer given with --renderer never falls back.
        if (::galaxy::launch_args().renderer == 0u && galaxy::render::found_no_device()) {
            start_again_on_another_renderer(reason.c_str());
        }
#endif
        die("the renderer did not start%s%s", reason.empty() ? "" : ": ", reason.c_str());
    }
    boot_mark("renderer started");
    // While the start runs ahead, the frames' presents alone reach the screen (svc_frame_started).
    galaxy::render::set_frame_end_presents(!gBootFast.load(std::memory_order_relaxed));
    gAudio = new galaxy::host::NativeGameAudio();  // lives as long as the process (the audio clock is detached)
    // The live controls' links to the window: the mouse as the pointer, keyboard capture and controller navigation
    // while the settings overlay or the HOME Menu is open.
    galaxy::host::install_host_pointer_provider([]() noexcept {
        galaxy::host::HostPointerState state{};
        const galaxy::render::HostPointerState pointer = galaxy::render::poll_host_pointer_state();
        state.window_focused = pointer.window_focused;
        state.inside_client = pointer.inside_client;
        state.absolute_valid = pointer.absolute_valid && pointer.client_width > 1 && pointer.client_height > 1;
        state.left_button = pointer.left_button;
        state.right_button = pointer.right_button;
        state.client_x = pointer.client_x;
        state.client_y = pointer.client_y;
        state.client_width = pointer.client_width;
        state.client_height = pointer.client_height;
        state.absolute_sequence = pointer.absolute_sequence;
        state.debug_window = pointer.debug_window;
        return state;
    });
    galaxy::host::install_host_pointer_transition_provider(
        [](std::uint64_t after) noexcept { return galaxy::render::poll_host_pointer_transition(after); });
    galaxy::host::install_native_keyboard_capture_provider([]() noexcept { return galaxy::render::native_window_input_captured(); });
    galaxy::host::install_native_settings_pad_hooks(
        []() noexcept {
            // A benchmark's panel takes the pad as a menu does: B stops the run (native_settings.cpp).
            return galaxy::render::native_settings_overlay_visible() || galaxy::render::home_menu_visible() ||
                galaxy::ui::launcher_visible() || galaxy::bench::running();
        },
        [](std::uintptr_t key) noexcept { galaxy::render::native_settings_post_key(key); });
    // A start goes straight into the game; --home 1 shows the home screen first. Its Benchmark and the settings menu's
    // Run benchmark run in the game.
    galaxy::ui::launcher_offer_benchmark(!benchmark);
    if (!benchmark) {
        galaxy::ui::benchmark_menu::set_runner(
            {start_benchmark_here, []() { return gInPlay.load(std::memory_order_relaxed); }});
        // The HDR calibration's reference stage, from the title or the save select: nothing of the player's is in play
        // there, and the game's reset works once the title has shown (not at the boot, nor under the home screen).
        galaxy::calibration_scene::set_gate([]() {
            return gTitleShown.load(std::memory_order_relaxed) && !gInPlay.load(std::memory_order_relaxed) &&
                   !galaxy::bench::running() && !galaxy::ui::launcher_visible();
        });
    }
#if defined(NEBULA_DIAG_REPORTS)
    // Send to developer, where the build has the developer's endpoint; What is sent where the system has a viewer.
    if (galaxy::diag::uploads_enabled()) {
        galaxy::ui::benchmark_menu::Reports reports;
        reports.send = send_benchmark_report;
#if defined(NEBULA_HOST_SHOW_REPORT)
        reports.show = show_benchmark_report;
#endif
        galaxy::ui::benchmark_menu::set_reports(reports);
    }
#endif
    const bool home_screen = home == 1;
    galaxy::ui::launcher_enable(home_screen);
#if defined(NEBULA_UPDATE_CHECK)
    // The update check (update.h; off with Check for updates at start) runs while the game boots: its answer shows on the
    // home screen, else over the title when the player presses a button there (NEBULA_HOME_MENU_LAUNCHER_NOTICE). A
    // benchmark, a hidden start and a timed run check nothing.
    if (!benchmark && !::galaxy::launch_args().started_hidden && seconds <= 0) galaxy::update::begin(NEBULA_VERSION);
#endif
#endif
    if (gModule.api_fingerprint == nullptr || gModule.api_fingerprint() != NEBULA_GAME_API_FINGERPRINT) {
        die("the game module was built from another nebula_game_api.h than the host (%08x): build both again",
            static_cast<unsigned>(NEBULA_GAME_API_FINGERPRINT));
    }
    if (gModule.attach(&services) != NEBULA_GAME_API_VERSION) {
        die("the game module's API version differs from the host's (%u)", NEBULA_GAME_API_VERSION);
    }

    name_thread(L"Nebula Host Main");
    gFs.start();
#if !defined(NEBULA_HOST_RENDERER)
    std::thread(sound_clock_thread).detach();
#endif
#if defined(NEBULA_HOST_RENDERER)
    std::thread(input_thread).detach();
#endif
#if defined(NEBULA_HOST_SDL)
    galaxy::sdl::start_thread_sampler();
#endif
    std::thread game([] {
        name_thread(L"Nebula Game");
        galaxy::host::perf_hint_add_current_thread();
        gModule.run();
    });
#if defined(_WIN32)
    if (::galaxy::diagnosing(::galaxy::kDiagnoseProfile)) {
        game_profile::start(GetThreadId(static_cast<HANDLE>(game.native_handle())));
    }
    // The simulation thread's CPU time, reported each second (the game and everything it calls on that thread).
    HANDLE game_thread = nullptr;  // its own handle: detach() closes the thread object's
    DuplicateHandle(GetCurrentProcess(), static_cast<HANDLE>(game.native_handle()), GetCurrentProcess(), &game_thread,
                    THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
    uint64_t game_cycles_last = 0;
    const bool trace_thread_cpu = ::galaxy::diagnosing(::galaxy::kDiagnoseThreadCpu);
    // Thread cycles count at the time-stamp counter's rate, measured here against the performance counter.
    const double tsc_hz = !trace_thread_cpu ? 0.0 : [] {
        LARGE_INTEGER f, a, b;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        const uint64_t t0 = __rdtsc();
        do {
            QueryPerformanceCounter(&b);
        } while (b.QuadPart - a.QuadPart < f.QuadPart / 20);
        return (double)(__rdtsc() - t0) * (double)f.QuadPart / (double)(b.QuadPart - a.QuadPart);
    }();
#endif
    game.detach();
    boot_mark("game started");

    const auto end = Clock::now() + std::chrono::duration<double>(seconds > 0 ? seconds : 1e9);
    bool first_frame = false;
    uint32_t reported = 0;
    auto next_report = Clock::now() + std::chrono::seconds(1);
#if defined(_WIN32)
    // A test run's stall: the game made no frame for kStall while in sight. Once per run, every thread's stack is
    // dumped from outside (crash_watch, nebula-hang-*.dmp beside the crash dumps), for d/tools/read_dump.py; the run
    // goes on, and the runner fails it for the stall. Test runs only (an extension loaded): a player's long stop (a
    // debugger, a sleeping laptop) is not a fault worth a file.
    constexpr auto kStall = std::chrono::seconds(10);
    const bool watch_stalls = !::galaxy::launch_args().extensions.empty();
    uint32_t stall_frames = 0;
    auto stall_since = Clock::now();
    bool stall_dumped = false;
#endif
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
    gHostWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    galaxy::render::host_window::set_state_wake(gHostWake);
#endif
    while (Clock::now() < end && !gTourDone.load(std::memory_order_relaxed)) {
#if defined(NEBULA_HOST_RENDERER)
        // The window's messages, on the thread that created it. Every 8 ms at most: a controller's state is at most that
        // old when the game reads it once a frame, and a phone's main thread wakes 125 times a second, not 250.
#if defined(NEBULA_HOST_SDL)
        galaxy::sdl::pump_events(8);
        if (const bool away = galaxy::sdl::in_background(); away != gSuspended) {
            {
                std::lock_guard<std::mutex> lock(gSuspendLock);
                gSuspended = away;
            }
            gSuspendWake.notify_all();
            gAudio->set_paused(away);
            log_line("host:", away ? "suspended (out of sight)" : "resumed");
        }
        if constexpr (galaxy::kDesktopWindow) {
            // A desktop window not in front: the player's ON LOSING FOCUS (follow_focus_loss). Minimised is in_background,
            // which holds the game above.
            const bool minimised = galaxy::sdl::in_background();
            follow_focus_loss(minimised || !galaxy::sdl::input_state().window_focused, minimised);
        }
        // The system asked for memory (Android's onTrimMemory, macOS's memory pressure): what comes back by itself goes.
        // The store's view pages return from the file cache on the next read; the C heap's free pages are new pages when
        // used again.
        if (galaxy::sdl::take_low_memory()) {
            gPak.store.release_pages();
#if defined(__ANDROID__)
            (void)mallopt(M_PURGE, 0);
#endif
            log_line("host:", "the system asked for memory: the store's pages and the C heap's free pages released");
        }
#else
        // Until a message, a change of the window or the game's end (gHostWake), or the once-a-second report.
        {
            const auto until = std::chrono::duration_cast<std::chrono::milliseconds>(next_report - Clock::now()).count();
            const DWORD wait = first_frame ? static_cast<DWORD>(std::clamp<long long>(until + 1, 0, 1000)) : 5u;
            MsgWaitForMultipleObjects(1, &gHostWake, FALSE, wait, QS_ALLINPUT);
        }
        // Minimised: the game and its sound stop, as on the platforms that suspend, and the renderer presents nothing of its
        // own. The game's clock starts again from the return (svc_frame_wait: no step is owed), and the device is kept, so
        // showing the window again rebuilds nothing. A covered window is not this: the game and its sound go on, and the
        // renderer alone stops presenting (RenderCore, which looks at the window itself every 100 ms).
        // A window that is not in front: the player's ON LOSING FOCUS (follow_focus_loss).
        {
            const bool minimised_now = galaxy::render::host_window::is_minimized();
            const bool away = galaxy::render::host_window::is_background() || minimised_now;
            follow_focus_loss(away, minimised_now);
            // Behind another window (not minimised, which holds the game): the renderer shows the game's own frames only.
            galaxy::render::set_window_background(away && !minimised_now);
            apply_power_wish(away, minimised_now);
        }
        const bool minimised = galaxy::render::host_window::is_minimized();
        if (minimised != gSuspended) {
            galaxy::render::set_window_hidden(minimised);
            {
                std::lock_guard<std::mutex> lock(gSuspendLock);
                gSuspended = minimised;
            }
            gSuspendWake.notify_all();
            gAudio->set_paused(minimised);
            log_line("host:", minimised ? "suspended (minimised)" : "resumed");
        }
#endif
        galaxy::render::pump_messages();
        if (galaxy::render::quit_requested()) {
            break;
        }
        if (!first_frame && gSubmissions.load(std::memory_order_relaxed) != 0u) {
            first_frame = true;
            boot_mark("first game frame");
        }
        if (Clock::now() < next_report) {
            continue;
        }
#else
        std::this_thread::sleep_until(next_report);
#endif
        next_report += std::chrono::seconds(1);
        const uint32_t frames = gGameFrames.load(std::memory_order_relaxed);
#if defined(_WIN32)
        if (frames != stall_frames || frames == 0u || gSuspended) {
            stall_frames = frames;
            stall_since = Clock::now();
        } else if (watch_stalls && !stall_dumped && gCrashing.load() == 0 && Clock::now() - stall_since >= kStall) {
            // A crash being written is no stall: its threads wait for the crash's dump.
            stall_dumped = true;
            static wchar_t hang[1024];
            DWORD hang_error = 0;
            DWORD hang_snapshot = 0;
            const galaxy::crash_watch::Result written = galaxy::crash_watch::write_dump(
                GetCurrentThreadId(), nullptr,
                MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpIgnoreInaccessibleMemory, hang,
                sizeof(hang) / sizeof(hang[0]), &hang_error, &hang_snapshot, 30000u, L"nebula-hang");
            char text[MAX_PATH + 160];
            std::snprintf(text, sizeof(text), "the game made no frame for %lld s at frame %u: %s %ls (0x%08lX, snapshot 0x%08lX)",
                          static_cast<long long>(kStall.count()), frames,
                          written == galaxy::crash_watch::Result::kWritten ? "every thread's stack in" : "no stall dump",
                          fs::path(hang).filename().c_str(), hang_error, hang_snapshot);
            log_line("host:", text);
        }
#endif
        if (frames != reported) {
            char text[384];
            std::snprintf(text, sizeof(text), "frame %u, file reads %u, save ops %u, sound blocks %u, sound subframes %u",
                          frames,
                          gFileReads.load(std::memory_order_relaxed), gSaveOps.load(std::memory_order_relaxed),
                          gSoundBlocks.load(std::memory_order_relaxed), gSoundSubframes.load(std::memory_order_relaxed));
#if defined(NEBULA_HOST_RENDERER)
            if (frames / galaxy::kGameStepsPerSecond % 30 == 0) {
                log_line("host:", ("jaudio " + gAudio->stats_line()).c_str());
            }
            const size_t length = std::strlen(text);
            std::snprintf(text + length, sizeof(text) - length, ", renderer frames %llu",
                          (unsigned long long)gSubmissions.load(std::memory_order_relaxed));
#endif
#if defined(_WIN32)
            if (trace_thread_cpu) {
                report_thread_cpu(tsc_hz);
            }
            ULONG64 cycles = 0;
            if (trace_thread_cpu && QueryThreadCycleTime(game_thread, &cycles)) {
                const size_t used = std::strlen(text);
                std::snprintf(text + used, sizeof(text) - used, ", game cpu %.1f ms/s",
                              (double)(cycles - game_cycles_last) * 1000.0 / tsc_hz);
                game_cycles_last = cycles;
            }
#endif
            log_line("host:", text);
            reported = frames;
        }
    }
    log_line("host:", Clock::now() < end ? "the window was closed" : "time limit reached");
    log_line("host:", ("3D scenes " + std::to_string(gSceneBegins.load(std::memory_order_relaxed))).c_str());
#if defined(NEBULA_HOST_RENDERER)
    galaxy::loss::report();
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    galaxy::timeline::dump();
#endif
#if defined(NEBULA_HOST_RENDERER)
    // The settings menu's restart (host_window::restart_game): the window closed for it; the game starts again, as a
    // diagnostics run when it asked for one.
    if (const int wanted = galaxy::render::host_window::restart_wanted(); wanted != 0) {
        using galaxy::render::host_window::Restart;
        gRestartKind = wanted;
        if (wanted == static_cast<int>(Restart::Diagnostics)) {
            add_restart_argument("--diagnostics");
            add_restart_argument("1");
        }
        finish_and_exit(true);
    }
#if defined(NEBULA_UPDATE_CHECK) && defined(_WIN32)
    // The notice's Update: Nebula Setup installs it once the game has left.
    if (galaxy::ui::launcher_poll() == galaxy::ui::LauncherModel::kChoiceUpdate && !galaxy::update::start_setup_update()) {
        log_line("host:", "Nebula Setup could not start to install the update");
    }
#endif
#endif
    finish_and_exit(false);
}
