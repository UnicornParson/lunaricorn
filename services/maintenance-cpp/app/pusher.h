#pragma once

#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <utility>
#include "storage.h"

namespace lunaricorn
{
class Pusher
{
public:
    explicit Pusher(std::shared_ptr<PGStorage> storage);
    ~Pusher();

    void start();
    void stop();

    void push(MaintenanceLogRecord& record);

private:
    void threadLoop();

    EventQueue<MaintenanceLogRecord> queue_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::shared_ptr<PGStorage> storage_;
};
} // namespace lunaricorn