#include "tcpconn.h"
#include <unistd.h>
#include <iostream>

TcpConnection::TcpConnection(int fd, EventLoop* loop) 
    : fd_(fd), loop_(loop), state_(CONNECTED) {}

TcpConnection::~TcpConnection(){
    close();
}

void TcpConnection::set_data_callback(const DataCallback &cb){
    data_callback_ = cb;
}

void TcpConnection::set_close_callback(const CloseCallback &cb){
    close_callback_ = cb;
}

void TcpConnection::establish(){
    // use epoll to monitor this lambda
    loop_->add_fd(fd_, EPOLLIN | EPOLLRDHUP | EPOLLET, 
            [this](int fd, uint32_t events) { handle_events(fd, events); });
}

void TcpConnection::send(const char* data, size_t len) {
    if (state_ != CONNECTED) return;
    
    // 简单实现，实际应该使用写缓冲区
    ssize_t n = write(fd_, data, len);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            close();
        }
    }
}

void TcpConnection::close() {
    if (state_ == CLOSED) return;
    
    state_ = CLOSED;
    loop_->remove_fd(fd_);
    ::close(fd_);
    
    if (close_callback_) {
        close_callback_(shared_from_this());
    }
}

int TcpConnection::fd() const { return fd_; } 

void TcpConnection::handle_events(int fd, uint32_t events) {
    if (events & EPOLLRDHUP || events & EPOLLHUP || events & EPOLLERR) {
        close();
        return;
    }
    
    if (events & EPOLLIN) {
        handle_read();
    }
}
    
void TcpConnection::handle_read() {
    char buffer[4096];
    ssize_t n;
    
    while (true) {
        n = read(fd_, buffer, sizeof(buffer));
        if (n > 0) {
            if (data_callback_) {
                data_callback_(shared_from_this(), buffer, n);
            }
        } else if (n == 0) {
            close();
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 没有更多数据可读
            } else {
                close();
                break;
            }
        }
    }
}
