#include "tcpserver.h"
#include "util.h"
#include <iostream>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

TcpServer::TcpServer(const char *ip, int port, int io_thread_num)
    : listen_fd_(create_and_bind(ip, port)),
      io_thread_num_(io_thread_num),
      next_loop_index_(0),
      timeout_manager_(300, [this](int fd)
                       { handle_timeout(fd); })
{

    set_nonblocking(listen_fd_);

    // main thread & main loop
    // only handle accept event
    // async-style, each time an event triggered, call the callback function
    main_loop_.add_fd(listen_fd_, EPOLLIN | EPOLLET,
                      [this](int fd, uint32_t events)
                      { handle_accept(fd, events); });

    main_thread_ = std::thread([this] {
        main_loop_.run();  
    });

    // create threadpool for non-blocking network io
    auto threadpool_ = std::make_unique<ThreadPool>(io_thread_num_);
    for (int i = 0; i < io_thread_num_; ++i)
    {
        loops_.emplace_back(new EventLoop());
        // eventloop is not running until we add_fd for it.
        threadpool_->enqueue([this, i]
                              { loops_[i]->run(); });

    }

    // allocate timeout thread
    timeout_thread_ = std::thread([this]{
        while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                timeout_manager_.check_timeouts();
        }
    });

}

TcpServer::~TcpServer(){
    stop();
}

void TcpServer::start(){
    running_ = true;
    main_loop_.run();
}

void TcpServer::stop(){
    running_ = false;
    main_loop_.stop();
    
    for (auto& loop : loops_) {
        loop->stop();
    }
    
    threadpool_->shutdown();
    
    // if (timeout_thread_.joinable()) timeout_thread_.join();
    
    close(listen_fd_);
}


void TcpServer::set_connection_callback(std::function<void(std::shared_ptr<TcpConnection>)> cb) {
    connection_callback_ = cb;
}

void TcpServer::set_message_callback(typename TcpConnection::DataCallback cb) {
    message_callback_ = cb;
}

void TcpServer::handle_accept(int fd, uint32_t events) {
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    int conn_fd;
    
    while ((conn_fd = accept4(fd, (sockaddr*)&client_addr, &addr_len, SOCK_NONBLOCK)) != -1) {
        if (conn_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 没有更多连接
            } else {
                perror("accept4");
                continue;
            }
        }
        
        // 打印客户端信息
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        std::cout << "Accepted connection from " << ip_str 
                    << ":" << ntohs(client_addr.sin_port) << std::endl;
        
        // 使用Round-Robin选择事件循环
        EventLoop* loop = get_next_loop();
        
        // 创建TCP连接
        auto conn = std::make_shared<TcpConnection>(conn_fd, loop);
        conn->set_data_callback(message_callback_);
        conn->set_close_callback([this](auto conn) {
            handle_close(conn);
        });
        
        // 添加到超时管理器
        timeout_manager_.add_connection(conn_fd);
        
        // 调用连接回调
        if (connection_callback_) {
            connection_callback_(conn);
        }
        
        // 在事件循环中建立连接
        conn->establish();

        addr_len = sizeof(client_addr); // 重置长度
    }
}

void TcpServer::handle_close(std::shared_ptr<TcpConnection> conn) {
    timeout_manager_.remove_connection(conn->fd());
}

void TcpServer::handle_timeout(int fd) {
    std::cout << "Connection timeout, closing fd: " << fd << std::endl;
    // 在实际实现中，应该通过事件循环关闭连接
    // 这里简化处理直接关闭
    ::close(fd);
}

EventLoop* TcpServer::get_next_loop() {
    // Round-Robin算法选择事件循环
    int index = next_loop_index_.fetch_add(1) % io_thread_num_;
    return loops_[index].get();
}