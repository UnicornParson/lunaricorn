#pragma once
#include "stdafx.h"
#include <atomic>
#include <mutex>
#include <deque>
#include <chrono>
#include <boost/json.hpp>

namespace lunaricorn
{

// Forward declarations for json serialization
namespace internal {
    class TelemetryData;
}

/**
 * @brief Global telemetry singleton for collecting service-wide statistics.
 *
 * Collects metrics such as:
 *   - total successful pushes
 *   - active clients count
 *   - pushes per last minute
 *   - errors per last minute
 *
 * Provides periodic logging via MLOG_D and JSON snapshot via to_json().
 * Not coupled to any specific endpoint — can be used from all parts of the service.
 */
class Telemetry
{
public:
    /// Returns the singleton instance (thread-safe, lazy init via C++11 magic static).
    static Telemetry& instance();

    // Non-copyable, non-movable
    Telemetry(const Telemetry&) = delete;
    Telemetry& operator=(const Telemetry&) = delete;
    Telemetry(Telemetry&&) = delete;
    Telemetry& operator=(Telemetry&&) = delete;

    // ---- Metric recording methods ----

    /// Record a successful push event (increments total counter and sliding window).
    void recordPushSuccess();

    /// Record a protocol error (increments error sliding window).
    void recordError();

    /// Set the current number of active clients (e.g. from RawEndpoint's client map size).
    void setActiveClients(size_t count);

    // ---- Query methods ----

    /// Total successful pushes since process start.
    uint64_t totalPushOk() const;

    /// Current active clients count.
    size_t activeClients() const;

    /// Number of successful pushes in the last 60 seconds.
    uint64_t pushesLastMinute() const;

    /// Number of errors in the last 60 seconds.
    uint64_t errorsLastMinute() const;

    /// Return a JSON object containing all current metrics.
    boost::json::object toJson() const;

    /// Print a one-line report via MLOG_D (periodically called from main loop).
    void printReport();

private:
    Telemetry() = default;
    ~Telemetry() = default;

    /// Evict entries older than 60 seconds from a sliding window deque.
    static void evictOld(std::deque<std::chrono::steady_clock::time_point>& dq);

    // ---- State ----
    std::atomic<uint64_t> _totalPushOk{ 0 };
    std::atomic<size_t>   _activeClients{ 0 };

    mutable std::mutex _mutex;
    std::deque<std::chrono::steady_clock::time_point> _pushTimestamps;
    std::deque<std::chrono::steady_clock::time_point> _errorTimestamps;

    static constexpr auto WINDOW_DURATION = std::chrono::seconds(60);
}; // class Telemetry

} // namespace lunaricorn