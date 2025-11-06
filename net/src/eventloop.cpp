#include "eventloop.h"
#include <system_error>
#include <iostream>
#include <unistd.h>

EventLoop::EventLoop() : running_(false) {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        throw std::system_error(errno, std::system_category(), "epoll_create1 failed");
    }
}

EventLoop::~EventLoop(){
    stop();
    close(epoll_fd_);
}

// add fd to be monitored by this epoll instance
void EventLoop::add_fd(int fd, uint32_t events, const EventCallback& cb){
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        throw std::system_error(errno, std::system_category(), "epoll_ctl add failed");
    }
    // register the callback function to this fd (hashmap)
    callbacks_[fd] = cb;
}

void EventLoop::modify_fd(int fd, uint32_t events){

    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        throw std::system_error(errno, std::system_category(), "epoll_ctl mod failed");
    }
}

void EventLoop::remove_fd(int fd){
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        // 忽略错误，可能fd已关闭
    }
    callbacks_.erase(fd);
}

void EventLoop::run(){
    running_ = true;
    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];
    
    while (running_) {
        // TODO: use suitable wait mode for your need
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100); // 100ms超时
        
        if (n == -1) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::system_category(), "epoll_wait failed");
        }
        
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t revents = events[i].events;
            
            if (callbacks_.count(fd)) {
                try {
                    callbacks_[fd](fd, revents);
                } catch (const std::exception& e) {
                    std::cerr << "Event callback error: " << e.what() << std::endl;
                }
            }
        }
    }
}

void EventLoop::stop() {
    running_ = false;
}