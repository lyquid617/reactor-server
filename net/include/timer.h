#pragma once

#include <chrono>
#include <functional>
#include <queue>
#include <unordered_map>
#include <mutex>

class ConnectionTimeoutManager {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Callback = std::function<void(int)>;
    
    ConnectionTimeoutManager(int timeout_seconds, Callback cb);
    void add_connection(int fd);
    void update_connection(int fd);
    void remove_connection(int fd);
    void check_timeouts();
    
private:
    struct TimeoutEntry {
        int fd;
        TimePoint expiry;
    };
    
    struct Compare {
        bool operator()(const TimeoutEntry& a, const TimeoutEntry& b) {
            return a.expiry > b.expiry;
        }
    };
    
    int timeout_seconds_;
    Callback callback_;
    std::mutex mutex_;
    // shared resources
    std::priority_queue<TimeoutEntry, std::vector<TimeoutEntry>, Compare> timeout_queue_;
    std::unordered_map<int, TimePoint> fd_to_expiry_;
};

