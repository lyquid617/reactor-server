#include "channel.h"


Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)   {   }

// Channel is owned by TCPConnection
// managed by epoller of eventloop
// so when deleting, check if removed from epoller/eventloop
Channel::~Channel(){
}

void Channel::tie(const std::shared_ptr<void> &conn){
    tie_ = conn;    // copy-assigned by the obj to monitored
    tied_ = true;
}

/**
 * guard from: 
 * 1. pending events on fd after conn is deleted
 * 2. fd is reused
 * 3. other reactor close the conn
 */
void Channel::handleEvent(TimeStamp ts){
    if(tied_){
        auto guard = tie_.lock();
        if(guard){
            handleEventGuarded(ts);
        } // else conn is closed
    }else{
        handleEventGuarded(ts);
    }
}

void Channel::handleEventGuarded(TimeStamp ts){
    // hang up without data to read
    if( (revents_ & EPOLLHUP) && !(revents_ & EPOLLIN) ){
        if(closeCallBack_) closeCallBack_();
    }
    if( revents_ & EPOLLERR){
        if(errorCallBack_) errorCallBack_();
    }
    // EPOLLRDHUP means receiving 'EOF' to read
    if(revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP) ){
        if(readCallBack_) readCallBack_(ts);
    }

    if(revents_ & EPOLLOUT){
        if(writeCallBack_) writeCallBack_();
    }
}