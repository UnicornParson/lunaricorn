#pragma once
#include "../stdafx.h"
namespace lunaricorn
{
namespace internal
{
struct ThreadState
{
    std::atomic_bool finished = false;
};
struct OrphanThread
{
    std::jthread thread;
    std::shared_ptr<ThreadState> state;

    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point control_start;
    std::string name;

    bool timeoutReported = false;
};
class OrphanThreadManager 
{
public:
    static OrphanThreadManager& instance();
    OrphanThreadManager();
    ~OrphanThreadManager();
    OrphanThreadManager(const OrphanThreadManager&) = delete;
    OrphanThreadManager(OrphanThreadManager&&) = delete;

    void add(
        std::jthread thread,
        std::shared_ptr<ThreadState> state,
        std::chrono::milliseconds timeout,
        std::string name);

private:
    void monitorLoop(std::stop_token stoken);

    std::mutex mutex_;
    std::vector<OrphanThread> threads_;

    std::jthread monitor_;
};
} // namespace internal
} // namespace lunaricorn