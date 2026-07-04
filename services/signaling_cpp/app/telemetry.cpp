#include "telemetry.h"
#include <iostream>

#include <Poco/TimerCallback.h>

namespace lunaricorn
{

Telemetry& Telemetry::instance()
{
    static Telemetry inst;
    return inst;
}

void Telemetry::start()
{
    bool expected = false;
    if (!_running.compare_exchange_strong(expected, true))
    {
        MLOG_D("Telemetry: already running, ignoring start()");
        return;
    }

    // TimerCallback adapter: calls Telemetry::onTimer
    _timer.start(Poco::TimerCallback<Telemetry>(*this, &Telemetry::onTimer),
                 START_DELAY_MS, REPORT_INTERVAL_MS);
    MLOG_D("Telemetry: started, report interval={}ms", REPORT_INTERVAL_MS);
}

void Telemetry::stop()
{
    bool expected = true;
    if (!_running.compare_exchange_strong(expected, false))
    {
        MLOG_D("Telemetry: not running, ignoring stop()");
        return;
    }

    _timer.stop();
    MLOG_D("Telemetry: stopped");
}

void Telemetry::onTimer(Poco::Timer& /*timer*/)
{
    printReport();
}

void Telemetry::evictOld(std::deque<std::chrono::steady_clock::time_point>& dq)
{
    const auto now = std::chrono::steady_clock::now();
    while (!dq.empty() && (now - dq.front()) > WINDOW_DURATION) {
        dq.pop_front();
    }
}

void Telemetry::recordPushSuccess()
{
    _totalPushOk.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(_mutex);
    _pushTimestamps.push_back(std::chrono::steady_clock::now());
}

void Telemetry::recordError()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _errorTimestamps.push_back(std::chrono::steady_clock::now());
}

void Telemetry::setActiveClients(size_t count)
{
    _activeClients.store(count, std::memory_order_relaxed);
}

uint64_t Telemetry::totalPushOk() const
{
    return _totalPushOk.load(std::memory_order_relaxed);
}

size_t Telemetry::activeClients() const
{
    return _activeClients.load(std::memory_order_relaxed);
}

uint64_t Telemetry::pushesLastMinute() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    evictOld(const_cast<std::deque<std::chrono::steady_clock::time_point>&>(_pushTimestamps));
    return static_cast<uint64_t>(_pushTimestamps.size());
}

uint64_t Telemetry::errorsLastMinute() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    evictOld(const_cast<std::deque<std::chrono::steady_clock::time_point>&>(_errorTimestamps));
    return static_cast<uint64_t>(_errorTimestamps.size());
}

boost::json::object Telemetry::toJson() const
{
    boost::json::object obj;
    obj["total_push_ok"]       = static_cast<int64_t>(totalPushOk());
    obj["active_clients"]      = static_cast<int64_t>(activeClients());
    obj["pushes_per_minute"]   = static_cast<int64_t>(pushesLastMinute());
    obj["errors_per_minute"]   = static_cast<int64_t>(errorsLastMinute());
    return obj;
}

void Telemetry::printReport()
{
    const uint64_t total  = totalPushOk();
    const size_t   active = activeClients();
    const uint64_t ppm    = pushesLastMinute();
    const uint64_t epm    = errorsLastMinute();

    MLOG_D("[TELEMETRY] total_push_ok={} active_clients={} pushes_1m={} errors_1m={}",
           total, active, ppm, epm);
}

} // namespace lunaricorn