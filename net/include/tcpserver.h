#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include "eventloop.h"
#include "tcpconn.h"
#include "timer.h"
#include "threadpool.h"

class TcpServer {
public:
    TcpServer(const char* ip, int port, int thread_num = std::thread::hardware_concurrency());
    ~TcpServer();
    
    void start();
    void stop();
    
    void set_connection_callback(std::function<void(std::shared_ptr<TcpConnection>)> cb);
    void set_message_callback(typename TcpConnection::DataCallback cb);
    
private:
    void handle_accept(int fd, uint32_t events);
    void handle_close(std::shared_ptr<TcpConnection> conn);
    void handle_timeout(int fd);
    EventLoop* get_next_loop();
    
    int listen_fd_;
    EventLoop main_loop_;
    std::thread main_thread_;
    int io_thread_num_;
    std::atomic<int> next_loop_index_;
    std::vector<std::unique_ptr<EventLoop>> loops_;

    std::unique_ptr<ThreadPool> threadpool_;
    std::atomic<bool> running_{false};
    
    ConnectionTimeoutManager timeout_manager_;
    std::thread timeout_thread_;
    
    std::function<void(std::shared_ptr<TcpConnection>)> connection_callback_;
    typename TcpConnection::DataCallback message_callback_;
};

#endif // TCPSERVER_H