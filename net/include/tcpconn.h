#pragma once

#include <memory>
#include <functional>
#include "eventloop.h"

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using DataCallback = std::function<void(std::shared_ptr<TcpConnection>, const char*, size_t)>;
    using CloseCallback = std::function<void(std::shared_ptr<TcpConnection>)>;
    
    TcpConnection(int fd, EventLoop* loop);
    ~TcpConnection();
    
    void set_data_callback(const DataCallback& cb);
    void set_close_callback(const CloseCallback& cb);
    void establish();
    void send(const char* data, size_t len);
    void close();
    
    int fd() const;
    
private:
    void handle_events(int fd, uint32_t events);
    void handle_read();
    
    enum State { CONNECTED, CLOSED };
    
    int fd_;
    EventLoop* loop_;
    State state_;
    DataCallback data_callback_;
    CloseCallback close_callback_;
};

