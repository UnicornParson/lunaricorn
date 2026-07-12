#include "oid.h"

#include <string>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <random>
#include <format>
#include <mutex>
#include <random>
using namespace std::chrono;

std::string make_oid()
{    
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    // --- 1. Наносекунды UTC (C++20 utc_clock) ---
    auto now = utc_clock::now();
    auto ns = duration_cast<nanoseconds>(now.time_since_epoch()).count();
    uint64_t value = static_cast<uint64_t>(ns);

    // --- 2. Кодирование в Base62 ---
    constexpr char alphabet[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    constexpr size_t base = sizeof(alphabet) - 1; // 62

    std::string base62;
    if (value == 0) {
        base62 = "0";
    } else {
        base62.reserve(11);
        while (value > 0) {
            base62.push_back(alphabet[value % base]);
            value /= base;
        }
        std::reverse(base62.begin(), base62.end());
    }

    // --- 3. Генератор случайных чисел (инициализируется один раз) ---
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<int> dist(0, 9999);

    int rand_part = dist(rng);

    // --- 4. Формирование итогового ключа: Base62 + '-XXXX' ---
    return std::format("{}-{:04d}", base62, rand_part);
}

