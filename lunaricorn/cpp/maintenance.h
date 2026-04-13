#pragma once
#include "stdafx.h"

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/NetException.h>
#include <Poco/URI.h>
#include <Poco/StreamCopier.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Array.h>
#include <Poco/Dynamic/Var.h>

namespace lunaricorn
{
    class LogCollectorClient {
        public:
            // Singleton access
            static LogCollectorClient& instance();
        
            // Delete copy/move
            LogCollectorClient(const LogCollectorClient&) = delete;
            LogCollectorClient& operator=(const LogCollectorClient&) = delete;
            LogCollectorClient(LogCollectorClient&&) = delete;
            LogCollectorClient& operator=(LogCollectorClient&&) = delete;
        
            // API methods
            Poco::JSON::Object::Ptr get_status();
            bool health_check();
            Poco::JSON::Object::Ptr send_log(std::string_view owner,
                                             std::string_view token,
                                             std::string_view message,
                                             std::string_view log_type,
                                             const std::optional<std::string>& datetime = std::nullopt);
            Poco::JSON::Object::Ptr send_logs_batch(const std::vector<Poco::JSON::Object::Ptr>& logs);
            std::vector<Poco::JSON::Object::Ptr> pull_logs(int offset = 0);
            std::string pull_logs_plain(int offset = 0);
            std::pair<std::string, std::string> download_logs_plain(int offset = 0);
        
            // Resource cleanup
            void close();
        
            // Configuration getters
            const std::string& base_url() const { return base_url_; }
            int timeout_seconds() const { return timeout_; }
            int max_retries() const { return max_retries_; }
            std::chrono::milliseconds retry_delay() const { return retry_delay_; }
            int retries_requested() const { return retries_requested_; }
        
        private:
            // Private constructor for singleton
            explicit LogCollectorClient(std::string base_url,
                                        int timeout_sec = 10,
                                        int max_retries = 3,
                                        std::chrono::milliseconds retry_delay = std::chrono::milliseconds(100));
        
            // Internal helpers
            std::string build_url(std::string_view path) const;
            Poco::JSON::Object::Ptr handle_json_response(Poco::Net::HTTPResponse& response, std::istream& body_stream);
            std::string handle_text_response(Poco::Net::HTTPResponse& response, std::istream& body_stream);
            std::string extract_filename_from_content_disposition(const std::string& header) const;
        
            template<typename Func>
            auto with_retry(Func&& operation) -> decltype(operation());
        
            // Configuration
            std::string base_url_;
            int timeout_;
            int max_retries_;
            std::chrono::milliseconds retry_delay_;
            mutable int retries_requested_{0};
        
            // Thread safety for operations that require serialisation (not used if sessions are per-request)
            std::mutex mutex_;
        };
        
        // Template implementation
        template<typename Func>
        auto LogCollectorClient::with_retry(Func&& operation) -> decltype(operation()) {
            std::exception_ptr last_exception;
            for (int attempt = 1; attempt <= max_retries_; ++attempt) {
                try {
                    return operation();
                } catch (const Poco::Net::ConnectionRefusedException& e) {
                    last_exception = std::current_exception();
                    retries_requested_++;
                    if (attempt < max_retries_) {
                        std::this_thread::sleep_for(retry_delay_);
                    }
                } catch (const Poco::Net::NetException& e) {
                    last_exception = std::current_exception();
                    retries_requested_++;
                    if (attempt < max_retries_) {
                        std::this_thread::sleep_for(retry_delay_);
                    }
                } catch (const Poco::TimeoutException& e) {
                    last_exception = std::current_exception();
                    retries_requested_++;
                    if (attempt < max_retries_) {
                        std::this_thread::sleep_for(retry_delay_);
                    }
                } catch (...) {
                    throw;
                }
            }
            std::rethrow_exception(last_exception);
        }
};
