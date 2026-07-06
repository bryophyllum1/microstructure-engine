#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace msengine {

// Lock-free single-producer/single-consumer ring buffer.
//
// Exactly ONE thread may call try_push and exactly ONE thread may call
// try_pop. Under that contract no locks are needed: the producer only
// writes `tail_`, the consumer only writes `head_`, and each reads the
// other's index with acquire/release ordering.
//
// Design notes (the parts that matter for latency):
//
// * Capacity is rounded up to a power of two so `index & mask_` replaces
//   the modulo operation (integer division) on every access.
//
// * head_ and tail_ live on separate cache lines (alignas below). If they
//   shared one line, every producer write would invalidate the consumer's
//   cached copy and vice versa — "false sharing" — turning two independent
//   counters into a ping-ponging contention point.
//
// * Each side keeps a cached copy of the other side's index and only
//   re-reads the shared atomic when the cache says the queue looks
//   full/empty. This cuts cross-core traffic dramatically: in the common
//   case a push touches no memory the consumer writes.
//
// * try_push/try_pop never block and never allocate. Overflow is the
//   caller's policy decision (drop + count, in our pipeline).
template <typename T>
class SpscQueue {
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "T must be nothrow move assignable for queue integrity");

public:
    explicit SpscQueue(std::size_t min_capacity)
        : mask_(round_up_pow2(min_capacity) - 1), buf_(mask_ + 1) {}

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // Producer thread only. Returns false if the queue is full.
    bool try_push(T&& item) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_cache_ > mask_) {  // looks full — refresh cache
            head_cache_ = head_.load(std::memory_order_acquire);
            if (tail - head_cache_ > mask_) return false;  // actually full
        }
        buf_[tail & mask_] = std::move(item);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only. Returns false if the queue is empty.
    bool try_pop(T& out) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_cache_) {  // looks empty — refresh cache
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (head == tail_cache_) return false;  // actually empty
        }
        out = std::move(buf_[head & mask_]);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    std::size_t capacity() const { return mask_ + 1; }

    // Approximate — racy by nature, for monitoring only.
    std::size_t size_approx() const {
        return tail_.load(std::memory_order_relaxed) -
               head_.load(std::memory_order_relaxed);
    }

private:
    static std::size_t round_up_pow2(std::size_t v) {
        std::size_t p = 1;
        while (p < v) p <<= 1;
        return p;
    }

#ifdef __cpp_lib_hardware_interference_size
    static constexpr std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
    static constexpr std::size_t kCacheLine = 64;
#endif

    const std::size_t mask_;
    std::vector<T> buf_;

    // Consumer-owned index + producer's cached copy of it.
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};
    alignas(kCacheLine) std::size_t tail_cache_{0};  // consumer's cache of tail_

    // Producer-owned index + consumer's cached copy of it.
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
    alignas(kCacheLine) std::size_t head_cache_{0};  // producer's cache of head_
};

}  // namespace msengine
