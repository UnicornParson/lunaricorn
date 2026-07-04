#pragma once
#include <cstdint>
#include <chrono>
#include <random>

namespace lunaricorn
{

/// Exponential backoff with full jitter for reconnect delays.
///
/// Formula (AWS blog: exponential backoff with jitter):
///   sleep = random(0, min(cap, base * 2^attempt))
///
/// Defaults:
///   - base: 1s
///   - max: 60s
///   - max_attempts: 0 (infinite)
///
class ReconnectStrategy
{
public:
    using Clock = std::chrono::steady_clock;

    explicit ReconnectStrategy(
        std::chrono::milliseconds base = std::chrono::milliseconds(1000),
        std::chrono::milliseconds max  = std::chrono::milliseconds(60000),
        int max_attempts = 0 // 0 = unlimited
    ) noexcept
        : _base(base)
        , _max(max)
        , _max_attempts(max_attempts)
        , _attempt(0)
    {
    }

    /// Reset the backoff state (call after successful connection).
    void reset() noexcept
    {
        _attempt = 0;
    }

    /// Return the delay before the next reconnect attempt,
    /// then increment the attempt counter.
    std::chrono::milliseconds next_delay() noexcept
    {
        if (_max_attempts > 0 && _attempt >= _max_attempts) {
            // No more attempts allowed — return max forever (caller should stop)
            return _max;
        }

        // 1. Compute exponential cap
        auto cap = _base;
        for (int i = 0; i < _attempt; ++i) {
            cap *= 2;
            if (cap >= _max) {
                cap = _max;
                break;
            }
        }
        if (cap > _max) cap = _max;

        // 2. Full jitter: random(0, cap)
        std::uniform_int_distribution<int64_t> dist(0, cap.count());
        auto delay = std::chrono::milliseconds(dist(_rng));

        ++_attempt;
        return delay;
    }

    /// Number of consecutive failed attempts (since last reset).
    int attempt() const noexcept { return _attempt; }

    /// Whether reconnect should keep trying (true if max not reached).
    bool should_retry() const noexcept
    {
        return _max_attempts == 0 || _attempt < _max_attempts;
    }

private:
    std::chrono::milliseconds _base;
    std::chrono::milliseconds _max;
    int _max_attempts;
    int _attempt;
    std::mt19937 _rng{ std::random_device{}() };
};

} // namespace lunaricorn