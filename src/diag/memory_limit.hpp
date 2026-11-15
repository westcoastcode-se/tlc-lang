// `--max-memory`: the most memory the compiler process may use, 1 GiB by default.
//
// A watchdog thread reads the process's private resident memory and its swap every 10 ms. Shared
// library code does not count. Past the limit it prints `compile-time memory limit exceeded`, at the
// comptime call that is running if there is one, and exits with status 1. A comptime call names
// itself with `MemoryPlace` while it runs.
//
// The command line starts the watchdog. The language server does not, and neither do the tests that
// compile in their own process.
#pragma once

#include <atomic>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace tlc {

inline constexpr uint64_t kDefaultMaxMemory = uint64_t{1} << 30;

namespace memory_detail {

struct State {
    std::mutex lock;
    // `path:line:column` of the running comptime call. Empty outside one.
    std::string place;
};

inline State &state() {
    static State instance;
    return instance;
}

// Private resident memory plus swap, from `/proc/self/status`. Empty where that file does not exist.
inline std::optional<uint64_t> used_bytes() {
    std::FILE *file = std::fopen("/proc/self/status", "r");
    if (file == nullptr) {
        return std::nullopt;
    }
    uint64_t total = 0;
    bool found = false;
    char line[256];
    while (std::fgets(line, sizeof line, file) != nullptr) {
        const std::string_view text(line);
        for (const std::string_view key : {std::string_view("RssAnon:"), std::string_view("VmSwap:")}) {
            if (text.starts_with(key)) {
                unsigned long long kib = 0;
                if (std::sscanf(line + key.size(), "%llu", &kib) == 1) {
                    total += static_cast<uint64_t>(kib) * 1024;
                    found = true;
                }
            }
        }
    }
    std::fclose(file);
    if (!found) {
        return std::nullopt;
    }
    return total;
}

} // namespace memory_detail

// Names the comptime call that runs while this object lives. Calls nest. The inner one is reported.
class MemoryPlace {
  public:
    explicit MemoryPlace(std::string place) {
        memory_detail::State &shared = memory_detail::state();
        const std::lock_guard<std::mutex> guard(shared.lock);
        saved_ = std::exchange(shared.place, std::move(place));
    }
    ~MemoryPlace() {
        memory_detail::State &shared = memory_detail::state();
        const std::lock_guard<std::mutex> guard(shared.lock);
        shared.place = std::move(saved_);
    }
    MemoryPlace(const MemoryPlace &) = delete;
    MemoryPlace &operator=(const MemoryPlace &) = delete;

  private:
    std::string saved_;
};

// `512K`, `64M`, `1G`, `2T`, or a byte count. A suffix is a power of 1024, in either case.
// `0` turns the limit off. Empty when the text is not one of those.
inline std::optional<uint64_t> parse_memory_size(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    uint64_t shift = 0;
    switch (text.back()) {
    case 'k':
    case 'K':
        shift = 10;
        break;
    case 'm':
    case 'M':
        shift = 20;
        break;
    case 'g':
    case 'G':
        shift = 30;
        break;
    case 't':
    case 'T':
        shift = 40;
        break;
    default:
        break;
    }
    if (shift != 0) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    uint64_t value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        const uint64_t digit = static_cast<uint64_t>(ch - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return std::nullopt;
        }
        value = value * 10 + digit;
    }
    if (shift != 0 && value > (UINT64_MAX >> shift)) {
        return std::nullopt;
    }
    return value << shift;
}

// Starts the watchdog once. `0` starts nothing.
inline void start_memory_limit(uint64_t limit) {
    static std::atomic<bool> started{false};
    if (limit == 0 || started.exchange(true) || !memory_detail::used_bytes()) {
        return;
    }
    std::thread([limit] {
        for (;;) {
            if (const std::optional<uint64_t> used = memory_detail::used_bytes(); used && *used > limit) {
                std::string text;
                {
                    memory_detail::State &shared = memory_detail::state();
                    const std::lock_guard<std::mutex> guard(shared.lock);
                    text = shared.place;
                }
                text += text.empty() ? "error: " : ": error: ";
                text += "compile-time memory limit exceeded\n";
                std::fwrite(text.data(), 1, text.size(), stderr);
                std::fflush(stderr);
                std::_Exit(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }).detach();
}

} // namespace tlc
