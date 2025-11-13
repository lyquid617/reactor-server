#pragma once

#include "noncopyable.h"

#include "logger.h"
#include "timestamp.h"
#include "eventloop.h"

#include <functional>
#include <memory>


enum class ChannelState{ INIT, POLLING, REMOVED };

/**
 * each channel owns a (socket) fd to monitor, 
 * manage the events concerned and callback
 * It is like an I/O channel, with I/O (and control) events on fd
 * 
 * In sum, a channel is a register fd
 *      capable of handling received events.
 * EventLoop only need to hand out events, dont care detail.
 */

class Channel : Noncopyable{   // exclusive ownership for resources
using EventCallBack = std::function<void()>;
using ReadEventCallBack = std::function<void(TimeStamp)>;
public:
    Channel(EventLoop *loop, int fd);
    ~Channel();

    void tie(const std::shared_ptr<void> &conn);
    void handleEvent(TimeStamp ts);

    // provide best performance for lvalue, rvalue and xvalue
    // double safety without compiler type check
    template<typename ReadCallBack>
    void setReadCallBack(ReadCallBack &&cb) { readCallBack_ = std::forward<ReadCallBack>(cb);}
    template<typename CallBack>
    void setWriteCallBack(CallBack &&cb) { writeCallBack_ = std::forward<CallBack>(cb);}
    template<typename CallBack>
    void setCloseCallBack(CallBack &&cb) { closeCallBack_ = std::forward<CallBack>(cb);}
    template<typename CallBack>
    void setErrorCallBack(CallBack &&cb) { errorCallBack_ = std::forward<CallBack>(cb);}
    

    // modify events
    void enableReading() { events_ |= (EPOLLIN | EPOLLPRI); update(); }  // see linux-man for epoll_ctl
    void enableWriting() { events_ |= EPOLLOUT; update(); }
    void disableReading() { events_ &= ~(EPOLLIN | EPOLLPRI); update(); }
    void disableWriting() { events_ &= ~EPOLLOUT; update(); }
    void disableAll()      { events_ &= 0; update(); }

    // monitor status 
    bool isNonEvent() const { return events_ | 0; }
    bool isReading() const { return events_ & (EPOLLIN | EPOLLPRI); }
    bool isWriting() const { return events_ & EPOLLOUT; }

    // member access
    int fd() const { return fd_; }
    int events() const {return events_; }

    

    void setRevents(int revt) { revents_ = revt; }
    
    // remove from epoller
    void remove(){
        loop_->removeChannel(this);
    }

    ChannelState state() const { return state_; }
    void setState(ChannelState state){ state_ = state; }

private:
    void update(){
        loop_->updateChannel(this);     // epoll_ctl 
    };
    void handleEventGuarded(TimeStamp ts);

    EventLoop *loop_;
    const int fd_;  // socket fd, owned by TCPConnection or TCPServer
                    // should use a weak_ptr to observe the lifetime of fd_
    std::weak_ptr<void> tie_;
    bool tied_;

    int events_;    // bitmask. see more at enum EPOLL_EVENTS;
    int revents_;   // returned active events
    
    ChannelState state_;
    

    ReadEventCallBack readCallBack_;
    EventCallBack writeCallBack_;
    EventCallBack closeCallBack_;
    EventCallBack errorCallBack_;
};