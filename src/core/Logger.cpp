#include "Logger.h"
#include <chrono>

Logger::Logger(std::function<void(const LogEntry&)> sink)
    : sink_(std::move(sink)) {
    running_.store(true, std::memory_order_release);
    sink_thread_ = std::thread(&Logger::sink_thread_fn, this);
}

Logger::~Logger() {
    flush();
    running_.store(false, std::memory_order_release);
    if (sink_thread_.joinable())
        sink_thread_.join();
}

void Logger::set_sink(std::function<void(const LogEntry&)> sink) {
    sink_ = std::move(sink);
}

void Logger::log(LogLevel level, uint64_t tick, const char* msg) {
    size_t w = write_idx_.load(std::memory_order_relaxed);
    size_t next_w = (w + 1) % RING_SIZE;

    // If ring is full, drop (never block the hot path).
    if (next_w == read_idx_.load(std::memory_order_acquire))
        return;

    LogEntry& e = ring_[w];
    e.level = level;
    e.tick  = tick;
    std::strncpy(e.msg, msg, sizeof(e.msg) - 1);
    e.msg[sizeof(e.msg) - 1] = '\0';

    write_idx_.store(next_w, std::memory_order_release);
}

void Logger::flush() {
    // Spin until consumer catches up.
    while (read_idx_.load(std::memory_order_acquire)
           != write_idx_.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void Logger::sink_thread_fn() {
    while (running_.load(std::memory_order_acquire)) {
        size_t r = read_idx_.load(std::memory_order_relaxed);
        size_t w = write_idx_.load(std::memory_order_acquire);

        while (r != w) {
            if (sink_) sink_(ring_[r]);
            r = (r + 1) % RING_SIZE;
            read_idx_.store(r, std::memory_order_release);
            w = write_idx_.load(std::memory_order_acquire);
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    // Drain any remaining entries after stop signal.
    size_t r = read_idx_.load(std::memory_order_relaxed);
    size_t w = write_idx_.load(std::memory_order_acquire);
    while (r != w) {
        if (sink_) sink_(ring_[r]);
        r = (r + 1) % RING_SIZE;
    }
    read_idx_.store(r, std::memory_order_release);
}
