#ifndef THREADSAFEQUEUE_H
#define THREADSAFEQUEUE_H

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>

// Many threads can push; typically one thread pops.
// Producers never touch the order book: they only push requests here.
template <typename T>
class ThreadSafeQueue {
public:
    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        cv_.notify_one();
    }

    // Non-blocking. Returns false if the queue is empty.
    bool tryPop(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    // Waits up to `timeout` for an item. Returns false if none arrived.
    bool waitPopFor(T& out, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); });
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop();
        return true;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
};

#endif
