#pragma once

#include "noncopyable.h"
#include "current_thread.h"
#include "channel.h"

#include <functional>
#include <unordered_map>
#include <atomic>
#include <sys/epoll.h>


class Channel;
/**
 * each eventloop manage an epoll fd
 */
class EventLoop : Noncopyable { /* exclusive ownership of epoll fd */
    public:
    
    EventLoop();
    ~EventLoop();

    void updateChannel(Channel *ch);
    void removeChannel(Channel *ch);
    bool hasChannel(Channel *ch);

    void wakeup();


    TimeStamp lastEpollTime(){ return lastEpollTime_; }
    // worker loop, default idle until binding a fd
    void run();
    void stop();

    // Ignore thread communication
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

    
private:
    using ChannelList = std::vector<Channel*>;
    using ChannelMap = std::unordered_map<int, Channel*>;
    using EventList = std::vector<epoll_event>;
    using Functor = std::function<void()>;

    int epollFd_;  //  we don't encapsulate epoller here
    void updateEpoller(int operation, Channel* ch);

    // use atomic state to support safe inter-thread management
    std::atomic<bool> looping_;
    std::atomic<bool> stop_;
    std::atomic<bool> doingPendingFunctors_;

    // worker thread, managed by main thread
    const pid_t threadId_;
    TimeStamp lastEpollTime_;
    // TODO: timer
    int wakeupFd_;
    std::unique_ptr<Channel> wakeupChannel_;    // exclusive ownership & lifetime management
    void handleWakeup();  // cb for wakeupfd events
    void doPendingFunctors();   

    ChannelMap channels_;
    ChannelList activeChannels_;
    EventList eventList_;   // buffer for revents
    enum class WAIT_MODE{
        BLOCKING = -1,
        BUSY_WAIT = 0,
        TIMEOUT = 100 // 100ms
    };
    const int kEventListSize = 64;

    std::mutex mutex_;      // mutex for inter-thread communication
    std::vector<Functor> pendingFunctors_;  // queue for async tasks
    // do I need to support args forwording?
};

