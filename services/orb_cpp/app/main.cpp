#include <iostream>
#include <lunaricorn.h>
#include <config.h>
#include <thread>
#include <chrono>

#include <csignal>
#include <pthread.h>
#include <atomic>
#include <stdexcept>
#include <random>
#include <leader_api.h>

constexpr std::string app_name { "orb" };
constexpr std::string app_ver { "0.2" };
constexpr std::string raw_host { "127.0.0.1" };
constexpr Poco::UInt16 raw_port = 8080;
constexpr std::string http_host { "0.0.0.0" };
constexpr Poco::UInt16 http_port = 8081;

using namespace lunaricorn;
using namespace std::chrono_literals; 


class SignalWaiter
 {
public:
    SignalWaiter()
    {
       sigemptyset(&set_);
       sigaddset(&set_, SIGTERM);
       sigaddset(&set_, SIGINT);
       sigaddset(&set_, SIGQUIT);
    
       if (pthread_sigmask(SIG_BLOCK, &set_, nullptr))
           throw std::runtime_error("signal block failed");
    
       MLOG_D("Signal waiter initialized");
    }
    

    int wait()
    {
        int sig{};
        if (sigwait(&set_, &sig))
            throw std::runtime_error("sigwait failed");
     
        stopped_ = true;
        MLOG_W("Shutdown signal received: {}", sig);
     
        return sig;
    }
     
    inline bool stopped() const { return stopped_; }

private:
    sigset_t set_{};
    std::atomic_bool stopped_{false};
};


std::string get_instance_identifier()
{
    auto now = std::chrono::system_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  now.time_since_epoch())
                  .count();
    pid_t pid = getpid();
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> dist(100000, 999999);
    int random6 = dist(gen);
    return std::format("#{}_{}_{}_{:06d}", app_ver, ns, pid, random6);
}

// Temporary engine stub — will be replaced with actual implementation
std::shared_ptr<void> make_engine(const DbConfig& cfg)
{
    MLOG_D("make_engine stub called with config: {}", cfg.toStr());
    return nullptr;
}

int main() {
    const std::string app_token = get_instance_identifier();
    MLog::owner = app_name;
    MLog::token = app_token;
    // MLog::is_stub = false;
    bool selftest_ok = false;
    MLOG_D("run {} {}", app_name, app_token);
    SignalWaiter signals;
    DbConfig dbcfg = loadConfigFromEnvironment();
    auto engine = make_engine(dbcfg);


    // Check TEST_MODE environment variable for testing
    const char* test_mode_env = std::getenv("TEST_MODE");
    bool test_mode = (test_mode_env != nullptr && test_mode_env[0] != '\0');

    std::string leader_url;
    bool leader_enabled = true;

    if (test_mode) {
        // Test mode: skip leader connection entirely
        MLOG_W("TEST_MODE is set, leader connection disabled");
        leader_enabled = false;
    } else {
        // Normal mode: CLUSTER_LEADER_URL is required
        const char* leader_url_env = std::getenv("CLUSTER_LEADER_URL");
        if (leader_url_env == nullptr) {
            MLOG_E("CLUSTER_LEADER_URL is required in normal mode (set TEST_MODE=1 to disable leader)");
            return -1;
        }
        leader_url = leader_url_env;
        MLOG_D("Leader URL: {}", leader_url);
    }

    // Create LeaderConnector and wait for leader readiness
    std::unique_ptr<LeaderConnector> leader;
    bool leader_ready = false;

    if (leader_enabled) {
        leader = ConnectorUtils::create_leader_connector(leader_url);

        // Wait for leader with periodic logging (every 60s) - interruptible by signals
        MLOG_D("Waiting for leader at {}...", leader_url);
        int wait_count = 0;
        while (!leader_ready) {
            try {
                // Use short timeout wait so we can check for shutdown signals.
                // wait_for_ready(5, 1) polls every 1 second internally
                // and returns after 5 seconds if leader is not ready.
                leader_ready = leader->wait_for_ready(5, 1);
                if (leader_ready) {
                    MLOG_D("Leader is ready");
                    break;
                }
            } catch (const std::exception& e) {
                MLOG_W("Failed to connect to leader: {}", e.what());
                // Continue retrying
            }

            wait_count++;
            // Log message every 60 seconds (assuming ~5s per attempt)
            if (wait_count % 12 == 0) {
                MLOG_W("Still waiting for leader at {}... (about {}s elapsed)", leader_url, wait_count * 5);
            }
        }

        if (leader_ready) {
            MLOG_D("Leader connection established");
        }
    }






    // Stop periodic registration on shutdown
    if (leader_enabled && leader_ready && leader) {
        try {
            leader->stop_registration_timer();
            MLOG_D("Leader registration timer stopped");
        } catch (...) {
            // ignore on shutdown
        }
    }

    return 0;
}
