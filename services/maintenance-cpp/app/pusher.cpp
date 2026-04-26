#include "pusher.h"
#include "stdafx.h"
namespace lunaricorn
{
Pusher::Pusher(std::shared_ptr<PGStorage> storage) : storage_(storage){}

Pusher::~Pusher()
{
    stop();
}

void Pusher::start()
{
    if (running_)
        return;

    running_ = true;
    worker_ = std::thread(&Pusher::threadLoop, this);
}

void Pusher::stop()
{
    if (!running_)
        return;

    running_ = false;

    if (worker_.joinable())
        worker_.join();
}

void Pusher::push(MaintenanceLogRecord& record)
{
    queue_.push(record);
}

void Pusher::threadLoop()
{
    using clock = std::chrono::steady_clock;

    std::uint64_t processedCount = 0;
    auto statTime = clock::now();

    while (running_)
    {
        auto events = queue_.get_all();
        std::optional<Poco::Int64> opt = storage_->push_all(events);
        processedCount += events.size();
        auto now = clock::now();

        if (now - statTime >= std::chrono::minutes(1))
        {
            double avgPerSecond =
                static_cast<double>(processedCount) / 60.0;

            std::cout
                << "Average processed messages: "
                << avgPerSecond
                << " msg/sec"
                << std::endl;

            processedCount = 0;
            statTime = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto events = queue_.get_all();

    if (!events.empty())
    {
        storage_->push_all(events);
    }
}

} // namespace lunaricorn