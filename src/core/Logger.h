#pragma once
#include <string>
#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>
#include <array>
#include <cstring>

// Lock-free SPSC ring-buffer logger.
// Sim thread (producer) enqueues log entries without touching disk.
// A dedicated sink thread (consumer) drains and writes.

enum class LogLevel : uint8_t { DEBUG, INFO, WARN, ERROR };

struct LogEntry {
    uint64_t tick;   // 8 bytes
    LogLevel level;  // 1 byte
    char msg[247];   // 247 bytes — total 256 bytes (no padding needed)
};
static_assert(sizeof(LogEntry) == 256, "LogEntry must be 256 bytes");

class Logger {
public:
    static constexpr size_t RING_SIZE = 1 << 14; // 16384 slots

    explicit Logger(std::function<void(const LogEntry&)> sink = {});
    ~Logger();

    // Producer API — called from sim thread.
    void log(LogLevel level, uint64_t tick, const char* msg);
    void log_info (uint64_t tick, const char* msg) { log(LogLevel::INFO,  tick, msg); }
    void log_warn (uint64_t tick, const char* msg) { log(LogLevel::WARN,  tick, msg); }
    void log_error(uint64_t tick, const char* msg) { log(LogLevel::ERROR, tick, msg); }

    // Flush all pending entries synchronously (call before shutdown).
    void flush();

    void set_sink(std::function<void(const LogEntry&)> sink);

private:
    void sink_thread_fn();

    std::array<LogEntry, RING_SIZE> ring_{};
    std::atomic<size_t> write_idx_{0};
    std::atomic<size_t> read_idx_{0};
    std::atomic<bool>   running_{false};
    std::function<void(const LogEntry&)> sink_;
    std::thread sink_thread_;
};

// Convenience macros — compile out in release hot path if needed.
#define LOG_INFO(logger, tick, msg)  (logger).log_info((tick),  (msg))
#define LOG_WARN(logger, tick, msg)  (logger).log_warn((tick),  (msg))
#define LOG_ERROR(logger, tick, msg) (logger).log_error((tick), (msg))
