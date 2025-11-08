#include "util.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <system_error>
#include <cstring>

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        throw std::system_error(errno, std::system_category(), "fcntl F_GETFL failed");
    }
    // file control, syscall, set flags with additional non-block
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::system_error(errno, std::system_category(), "fcntl F_SETFL failed");
    }
}

// create a fd, bind with an address, listen on it, return to caller
int create_and_bind(const char* ip, int port) {

    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd == -1) {
        throw std::system_error(errno, std::system_category(), "socket creation failed");
    }

    // 设置SO_REUSEADDR
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        close(listen_fd);
        throw std::system_error(errno, std::system_category(), "setsockopt SO_REUSEADDR failed");
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    // host to network short
    server_addr.sin_port = htons(port);
    
    if (ip == nullptr || strcmp(ip, "0.0.0.0") == 0) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        // presentation (string) to network (uint32_t)
        inet_pton(AF_INET, ip, &server_addr.sin_addr);
    }

    if (bind(listen_fd, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        close(listen_fd);
        throw std::system_error(errno, std::system_category(), "bind failed");
    }

    // syn queue + accept queue, 
    if (listen(listen_fd, SOMAXCONN) == -1) {
        close(listen_fd);
        throw std::system_error(errno, std::system_category(), "listen failed");
    }

    return listen_fd;
}