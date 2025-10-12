#include "timer.h"

ConnectionTimeoutManager::ConnectionTimeoutManager(int timeout_seconds, Callback cb) 
    : timeout_seconds_(timeout_seconds), callback_(cb) {}




void ConnectionTimeoutManager::add_connection(int fd){

    std::lock_guard<std::mutex> lock(mutex_);
    TimePoint expiry = std::chrono::steady_clock::now() + 
                        std::chrono::seconds(timeout_seconds_);
    timeout_queue_.push({fd, expiry});
    fd_to_expiry_[fd] = expiry;

}


void ConnectionTimeoutManager::update_connection(int fd){

    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_to_expiry_.count(fd)) {
        TimePoint expiry = std::chrono::steady_clock::now() + 
                            std::chrono::seconds(timeout_seconds_);
        fd_to_expiry_[fd] = expiry;
        // 简单实现：实际生产环境需要更高效的数据结构
        // 这里为了简单，我们每次检查时重建队列
    }
}



void ConnectionTimeoutManager::remove_connection(int fd){

    std::lock_guard<std::mutex> lock(mutex_);
    fd_to_expiry_.erase(fd);
    // 实际检查时会重建队列

}


void ConnectionTimeoutManager::check_timeouts(){

    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    
    // 重建优先级队列
    std::priority_queue<TimeoutEntry, std::vector<TimeoutEntry>, Compare> new_queue;
    
    while (!timeout_queue_.empty()) {
        auto entry = timeout_queue_.top();
        timeout_queue_.pop();
        
        // 如果fd还在映射中且过期时间已更新，使用新时间
        if (fd_to_expiry_.count(entry.fd)) {
            entry.expiry = fd_to_expiry_[entry.fd];
            new_queue.push(entry);
            
            if (entry.expiry <= now) {
                callback_(entry.fd);
                fd_to_expiry_.erase(entry.fd);
            }
        }
    }
    
    timeout_queue_ = std::move(new_queue);
}