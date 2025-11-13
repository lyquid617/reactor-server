#pragma once

#include <memory>
#include <functional>
#include <any>
#include "eventloop.h"
#include "socket.h"
#include "timestamp.h"
#include "callback.h"
#include "buffer/singletonBufferPool.h"

// owns a TCP socket, which is polled by an eventloop in a channel
// Created by server after accept()
class TcpConnection : Noncopyable,  public std::enable_shared_from_this<TcpConnection> {
public:
    
    TcpConnection(int fd, EventLoop* loop, const std::string &name,const InetAddress &localAddr,const InetAddress &clientAddr);
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_; }     // loop is owned by threadpool actually
    const std::string& name() const { return name_; }
    const InetAddress& localAddr() const { return localAddr_; }
    const InetAddress& clientAddr() const { return clientAddr_; }

    bool connected() const { return state_ == State::CONNECTED; }

    void setConnectionCallback(const ConnectionCallback &cb) { connectionCallback_ = cb; }
    void setReadDataCallback(const ReadDataCallback& cb) { readDataCallback_ = cb; }
    void setCloseCallback(const CloseCallback& cb) { closeCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }

    // add the connection fd to epoll fd list
    void establishConnection();
    // remove the connection fd from epoll fd set
    void destroyConnection();
    // shutdown the write end of the socket
    void shutdown();

    void send(const std::string &str);
    void send(const Buffer &buf);
    

    int fd() const { return socket_->fd(); };
    void setContext(const std::any &context) { context_ = context; }
    const std::any& getContext() const { return context_; }


private:
    // functions defined in tcp-conn is passed to channel
    void handleRead(TimeStamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    // eventloop management

    
    enum class State { DISCONNECTED, CONNECTED, CONNECTING, DISCONNECTING };
    void *setState(State state) {state_ = state;}

    
    EventLoop* loop_;
    std::unique_ptr<Socket> socket_;    // connection socket
    std::unique_ptr<Channel> channel_;

    const std::string name_;

    const InetAddress localAddr_;
    const InetAddress clientAddr_;

    std::atomic<State> state_;

    // called after connection established/destoryed    ( connection establish using connfd as an async procedure )
    ConnectionCallback connectionCallback_;
    // called after reading data
    ReadDataCallback readDataCallback_;
    // async structure, to eventloop thread
    // after data copied from user buffer to kernel buffer, do something
    WriteCompleteCallback writeCompleteCallback_;
    // called after connection closed
    CloseCallback closeCallback_;

    // high watermark
    HighWatermarkCallback highWaterMarkCallback_;
    size_t highWaterMark_;

    Buffer inputBuffer_;
    Buffer outputBuffer_;

    std::any context_;      // just like void* type context in c 
};

