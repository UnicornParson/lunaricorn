#include "thread_control.h"

#include "../maintenance.h"

namespace lunaricorn
{
namespace internal
{
OrphanThreadManager& OrphanThreadManager::instance()
{
    static OrphanThreadManager instance;
    return instance;
}


void OrphanThreadManager::monitorLoop(std::stop_token stoken)
{
    using namespace std::chrono;
    while (!stoken.stop_requested())
    {
        std::this_thread::sleep_for(1s);
        std::lock_guard lock(mutex_);
        const auto now = steady_clock::now();
        for (auto it = threads_.begin(); it != threads_.end();)
        {
            auto& t = *it;
            if (t.state->finished.load(std::memory_order_acquire))
            {
                t.thread.join();
                MLOG_D("Orphan thread '{}' finished in {} ms", t.name, duration_cast<milliseconds>(now - t.control_start).count());
                it = threads_.erase(it);
                continue;
            }

            if (now >= t.deadline)
            {
                const auto overtime = duration_cast<milliseconds>(now - t.deadline).count();
                if (!t.timeoutReported)
                {
                    MBUG("Orphan thread '{}' exceeded shutdown timeout by {} ms", t.name, overtime);
                    t.timeoutReported = true;
                }
            }
            ++it;
        }
    }

    std::lock_guard lock(mutex_);

    for (auto& t : threads_)
    {
        if (!t.state->finished.load(std::memory_order_acquire))
            MLOG_W("Waiting orphan thread '{}' during manager shutdown", t.name);
        // just remove jthread
        // t.thread.join();
    }

    threads_.clear();
}

void OrphanThreadManager::add(std::jthread thread,std::shared_ptr<ThreadState> state,std::chrono::milliseconds timeout,std::string name)
{
    if (!state)
    {
        MBUG("Attempt to add orphan thread '{}' with null state", name);
        return;
    }
    thread.request_stop();
    const auto now = std::chrono::steady_clock::now();
    if (state->finished.load(std::memory_order_acquire)) { return; } // good thread!

    std::lock_guard lock(mutex_);
    threads_.emplace_back(
        std::move(thread),
        std::move(state),
        (now + timeout), //deadline
        now, // control_start
        std::move(name),
        false);
}

OrphanThreadManager::OrphanThreadManager() : monitor_([this](std::stop_token stoken){ monitorLoop(stoken); })
{
    MLOG_D("OrphanThreadManager started");
}

OrphanThreadManager::~OrphanThreadManager()
{
    monitor_.request_stop();
    monitor_.join();
    std::lock_guard lock(mutex_);
    if (threads_.empty())
    {
        MLOG_D("OrphanThreadManager stopped");
        return;
    }

    MBUG("OrphanThreadManager destroyed with {} unfinished orphan threads", threads_.size());

    const auto now = std::chrono::steady_clock::now();

    for (const auto& t : threads_)
    {
        const auto lifetime = std::chrono::duration_cast<std::chrono::milliseconds>(now - t.control_start).count();
        const auto overtime = std::chrono::duration_cast<std::chrono::milliseconds>(now - t.deadline).count();

        MBUG("Unfinished orphan thread '{}', stop_wait={} ms, overtime={} ms, timeout_reported={}, finished={}",
            t.name,
            lifetime,
            std::max<int64_t>(0, overtime),
            t.timeoutReported,
            t.state->finished.load(std::memory_order_acquire));
    }
}

} // namespace internal
} // namespace lunaricorn