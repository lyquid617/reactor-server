#pragma once

#include "noncopyable.h"
#include "inetaddr.h"
#include "sys/socket.h"
#include <netinet/tcp.h>
#include "unistd.h"
#include "logger.h"

class InetAddress;

class Socket : Noncopyable{
public:
    explicit Socket(int sockfd) : sockfd_(sockfd) {}

    ~Socket(){      // auto lifetime management
        ::close(sockfd_);
    }

    int fd() { return sockfd_; }

    // socket standard operations
    void bindAddress(const InetAddress &localAddr){
        if(::bind(sockfd_, (sockaddr *)localAddr.getSockAddr(), sizeof(sockaddr_in))){
            LOG_FATAL << "bind sockfd :" << sockfd_ << " failed";
        }
    }
    void listen(){
        if(::listen(sockfd_, SOMAXCONN)){
            LOG_FATAL << "Listen sockfd :" << sockfd_ << " failed";
        }
    }

    // retrieve client address from the other end
    int accept(InetAddress *peerAddr){
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);

        int connFd = ::accept4(sockfd_, (sockaddr*)&clientAddr, &addrLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if(connFd >= 0){
            peerAddr->setSockAddr(clientAddr);
        }else{
            LOG_ERROR << "accept4() failed";
        }
        return connFd;
    }

    // socket options
    void shutdownWrite(){
        if(::shutdown(sockfd_, SHUT_WR) < 0){
            LOG_ERROR << "shutdownWrite() failed";
        }
    }

    void setTcpNoDelay(bool on){
        int optval = on;
        ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
    }

    // solve time wait
    void setReuseAddr(bool on){
        int optval = on;
        ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    }

    // share port with other process
    void setReusePort(bool on){
        int optval = on;
        ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    }
    
    // every 2 hour send a keep-alive packet
    void setKeepAlive(bool on){
        int optval = on;
        ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
    }



private:
    const int sockfd_;

};
