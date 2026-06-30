#pragma once

#include <csignal>
#include <pthread.h>
#include <atomic>
#include <stdexcept>

class SignalWaiter
 {
public:
    SignalWaiter();

    int wait();
    inline bool stopped() const { return stopped_; }

private:
    sigset_t set_{};
    std::atomic_bool stopped_{false};
};