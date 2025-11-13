#pragma once

#include <string>
#include <arpa/inet.h>
#include <netinet/in.h>

class InetAddress{
public:

    explicit InetAddress(uint16_t port = 0){
        addr_.sin_family = AF_INET;
        addr_.sin_port = ::htons(port);
        addr_.sin_addr.s_addr = ::htonl(INADDR_ANY);
    }
    explicit InetAddress(std::string ip, uint16_t port = 0){
        addr_.sin_family = AF_INET;
        addr_.sin_port = ::htons(port);
        addr_.sin_addr.s_addr = ::inet_addr(ip.c_str());
    }
    uint16_t toPort() const {
        return ::ntohs(addr_.sin_port);
    }

    std::string toIp() const{
        thread_local static char buf[32] = {};
        ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
        return buf;
    }
    std::string toIpPort() const{
        thread_local static char buf[64] = {};
        ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf) );
        uint16_t port = ::ntohs(addr_.sin_port);
        sprintf(buf + 32, ": %u", port);
        return buf;
    }
    void setSockAddr(const sockaddr_in addr) { addr_ = addr; }
    const sockaddr_in *getSockAddr() const { return &addr_; }


private:
    sockaddr_in addr_{};  // address family, port and address(32 bits), and padding to 15 bytes
};