#pragma once
#include "stdafx.h"
#include "config.h"
#include "maintenance.h"

namespace lunaricorn
{

std::string current_time_str();

#if DEBUG
#define DPRINT(s) std::cout << "[" << current_time_str() << "] point " << __FILE__ << " f:(" << __FUNCTION__ << ")." << __LINE__ << " : " << s << std::endl << std::flush
#define POINT std::cerr << "[" << current_time_str() << "] point " << __FILE__ << " f:(" << __FUNCTION__ << ")." << __LINE__ << std::endl << std::flush
#define PRINT_SESSION_COUNTER(prefix, sc) std::cout << "on session " << prefix << " count: " << (sc) << std::endl
#else
#define POINT 
#define DPRINT(s) 
#define PRINT_SESSION_COUNTER(prefix, sc)
#endif // DEBUG

#define LOG_ACCESS(msg) \
    do { \
        std::cout << "[" << current_time_str() << "] " << msg << std::endl << std::flush; \
    } while(0)


template <typename T>
class EventQueue {
public:
    void push(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(value);
    }
    void push(T&& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(value));
    }
    std::queue<T> get_all() {
        std::lock_guard<std::mutex> lock(mutex_);

        std::queue<T> queue_copy = std::move(queue_);
        queue_ = std::queue<T>{};

        return std::move(queue_copy);
    }

private:
    std::queue<T> queue_;
    std::mutex mutex_;
};


template <typename T>
struct Counted
{
    static inline std::atomic<uint64_t> count{0};

    Counted()   { count.fetch_add(1); }
    ~Counted()  { count.fetch_sub(1); }

    uint64_t alive_count() const { return count.load(); }
};
} // namespace lunaricorn