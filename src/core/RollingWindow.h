#pragma once
#include <vector>
#include <cassert>
#include <cstddef>
#include <cmath>
#include <limits>
#include <algorithm>

// Fixed-capacity circular buffer with O(1) incremental statistics.
// Variance computed via Welford's online algorithm (numerically stable).

template<typename T = double>
class RollingWindow {
public:
    explicit RollingWindow(size_t capacity)
        : capacity_(capacity), buf_(capacity, T{}) {
        assert(capacity > 0);
    }

    void push(T value) {
        if (count_ == capacity_) {
            // Sliding-window update: evict oldest, add new in one O(1) step.
            // Formula: mu_new = mu_old + (x_new - x_old) / K
            //          M2_new = M2_old + (x_new - x_old)(x_new + x_old - mu_old - mu_new)
            // Derivation: see Welford (1962) + Chan et al. (1979) sliding-window extension.
            T old = buf_[head_];
            double x_old = static_cast<double>(old);
            double x_new = static_cast<double>(value);
            double K     = static_cast<double>(capacity_);

            double old_mean = mean_;
            mean_ += (x_new - x_old) / K;
            m2_   += (x_new - x_old) * (x_new + x_old - old_mean - mean_);
            if (m2_ < 0.0) m2_ = 0.0;
            sum_ += x_new - x_old;

            buf_[head_] = value;
            head_ = (head_ + 1) % capacity_;
        } else {
            // Growing window: standard Welford forward.
            buf_[head_] = value;
            head_ = (head_ + 1) % capacity_;
            ++count_;

            double x = static_cast<double>(value);
            double delta = x - mean_;
            mean_ += delta / static_cast<double>(count_);
            double delta2 = x - mean_;
            m2_ += delta * delta2;
            sum_ += x;
        }
    }

    size_t size()     const { return count_; }
    size_t capacity() const { return capacity_; }
    bool   full()     const { return count_ == capacity_; }
    bool   empty()    const { return count_ == 0; }

    double mean()     const { return count_ > 0 ? mean_ : 0.0; }
    double sum()      const { return sum_; }

    // Population variance.
    double variance() const {
        return count_ > 1 ? m2_ / static_cast<double>(count_) : 0.0;
    }

    double stddev()   const { return std::sqrt(variance()); }

    // Youngest element.
    T back() const {
        assert(count_ > 0);
        size_t idx = (head_ + capacity_ - 1) % capacity_;
        return buf_[idx];
    }

    // Oldest element still in window.
    T front() const {
        assert(count_ > 0);
        size_t oldest = (head_ + capacity_ - count_) % capacity_;
        return buf_[oldest];
    }

    void reset() {
        count_ = 0; head_ = 0; mean_ = 0.0; m2_ = 0.0; sum_ = 0.0;
        std::fill(buf_.begin(), buf_.end(), T{});
    }

private:
    size_t capacity_;
    std::vector<T> buf_;
    size_t head_{0};
    size_t count_{0};
    double mean_{0.0};
    double m2_{0.0};
    double sum_{0.0};
};
