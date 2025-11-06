#pragma once

#include <functional>
#include <unordered_map>
#include <atomic>
#include <sys/epoll.h>

class EventLoop {
public:
    using EventCallback = std::function<void(int, uint32_t)>;
    
    EventLoop();
    ~EventLoop();
    
    void add_fd(int fd, uint32_t events, const EventCallback& cb);
    void modify_fd(int fd, uint32_t events);
    void remove_fd(int fd);
    // worker loop, default idle until binding a fd
    void run();
    void stop();
    
private:
    int epoll_fd_;
    // use atomic variable as global config share with threads
    std::atomic<bool> running_;
    std::unordered_map<int, EventCallback> callbacks_;
    enum class WAIT_MODE{
        BLOCKING = -1,
        BUSY_WAIT = 0,
        TIMEOUT = 1
    };
};

