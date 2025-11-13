#pragma once

#include "noncopyable.h"

#include <memory>
#include <unistd.h>
#include <sys/types.h>
#include <cstring>
#include <array>
#include <mutex>
#include <cstdlib>
#include <algorithm>
#include <stack>
#include <vector>
#include <cerrno>
#include <stdexcept>

// constructor should be protected, can't directly construct
class Buffer {
public:
    explicit Buffer(size_t size = 4096)
        : data_(new char[size]), capacity_(size), read_pos_(0), write_pos_(0) {}

    ~Buffer() = default;


    // expose capacity for pools
    size_t capacity() const { return capacity_; }

    char *data() const { return data_.get(); }


    // read data from socket fd -> this buffer 
    // buffer writer
    ssize_t readFromFD(int fd) {
        if (writableBytes() == 0) {
            // 尝试压缩或扩容以获得写空间
            ensureWritableBytes(1);
            if (writableBytes() == 0) return -1;
        }

        ssize_t n;
        for (;;) {
            n = ::read(fd, data_.get() + write_pos_, static_cast<size_t>(writableBytes()));
            if (n < 0) {
                if (errno == EINTR) continue; // 中断重试
                // 非阻塞且没有数据可读时返回 -1（errno 保持原样供上层判断）
                return -1;
            }
            break;
        }

        if (n > 0) write_pos_ += static_cast<size_t>(n);
        return n;
    }

    // 将缓冲区的数据写入 socket（支持 EINTR 重试）
    ssize_t writeToFD(int fd) {
        if (readableBytes() == 0) return 0;
        ssize_t n;
        for (;;) {
            n = ::write(fd, data_.get() + read_pos_, static_cast<size_t>(readableBytes()));
            if (n < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            break;
        }
        if (n > 0) {
            read_pos_ += static_cast<size_t>(n);
            if (read_pos_ == write_pos_) retrieveAll();
        }
        return n;
    }

    // producer position - consumer position
    size_t readableBytes() const { return write_pos_ - read_pos_; }

    // capacity - producer position
    size_t writableBytes() const { return capacity_ - write_pos_; }

    // bytes already read by us, can reuse
    size_t prependableBytes() const { return read_pos_; }

    // 获取读指针
    const char* readPtr() const { return data_.get() + read_pos_; }

    // 获取写指针
    char* writePtr() { return data_.get() + write_pos_; }

    // 移动读指针
    void retrieve(size_t len) {
        if (len < readableBytes()) {
            read_pos_ += len;
        } else {
            retrieveAll();
        }
    }

    // reset the pointers
    void retrieveAll() {
        read_pos_ = 0;
        write_pos_ = 0;
    }

    // 将数据追加到缓冲区，必要时压缩或扩容
    void append(const char* data, size_t len) {
        if (len == 0) return;
        ensureWritableBytes(len);
        if (writableBytes() < len) {
            // 如果仍然不足，视为失败（应该不会发生）
            throw std::runtime_error("Buffer append failed: insufficient space after ensureWritableBytes");
        }
        std::memcpy(data_.get() + write_pos_, data, len);
        write_pos_ += len;
    }

private:
    // 1. Consolidation the fragments ( recycle the space )
    // 2. allocate more space, at least double it.
    void ensureWritableBytes(size_t len) {
        if (writableBytes() >= len) return;

        size_t readable = readableBytes();
        if (prependableBytes() + writableBytes() >= len) {
            // 将未读数据移动到缓冲区前端
            if (readable > 0) {
                std::memmove(data_.get(), data_.get() + read_pos_, readable);
            }
            read_pos_ = 0;
            write_pos_ = readable;
        } else {
            // 扩容：扩大到 max(capacity*2, capacity + len)
            size_t new_capacity = std::max(capacity_ * 2, capacity_ + len);
            std::unique_ptr<char[]> new_data(new char[new_capacity]);
            if (readable > 0) {
                std::memcpy(new_data.get(), data_.get() + read_pos_, readable);
            }
            data_.swap(new_data);
            capacity_ = new_capacity;
            read_pos_ = 0;
            write_pos_ = readable;
        }
    }

    std::unique_ptr<char[]> data_;
    size_t capacity_;
    size_t read_pos_;
    size_t write_pos_;
};

class FixedSizePool : Noncopyable {
public:
    // manage shared_ptr<Buffer> with fixed capacity
    FixedSizePool(size_t block_size, size_t prealloc_count = 100)
        : block_size_(block_size) {
        preallocate(prealloc_count);
    }

    ~FixedSizePool() = default;

    // allocate returns a shared_ptr<Buffer> from pool or creates a new one
    std::shared_ptr<Buffer> allocate() {
        // lock for stack: free_list
        std::lock_guard<std::mutex> lock(mutex_);
        if (free_list_.empty()) {
            // create new Buffer objects to expand
            for (size_t i = 0; i < expand_size_; ++i) {
                free_list_.push(std::make_shared<Buffer>(block_size_));
            }
            expand_size_ = std::min(expand_size_ * 2, MAX_EXPAND_SIZE);
        }
        if (free_list_.empty()) return nullptr;
        auto buf = free_list_.top();
        free_list_.pop();
        return buf;
    }

    // return buffer to pool only if capacity matches
    void deallocate(std::shared_ptr<Buffer> buf) {
        if (!buf) return;
        if (buf->capacity() != block_size_) return; // ignore mismatched sizes
        std::lock_guard<std::mutex> lock(mutex_);
        // reset buffer pointers before returning
        buf->retrieveAll();
        free_list_.push(std::move(buf));
    }

    size_t block_size() const { return block_size_; }
    size_t free_block_count() const { return free_list_.size(); }

private:
    void preallocate(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            free_list_.push(std::make_shared<Buffer>(block_size_));
        }
    }

    size_t block_size_;
    size_t expand_size_ = 10;
    std::stack<std::shared_ptr<Buffer>> free_list_;
    std::mutex mutex_;

    static constexpr size_t MAX_EXPAND_SIZE = 1000;
};

// singleton instance
class BufferMemoryPool : Noncopyable {
public:
    static BufferMemoryPool& instance() {
        static BufferMemoryPool pool;
        return pool;
    }
    // only moveable
    class PooledBuffer : Noncopyable{
    public:
        PooledBuffer() = default;
        PooledBuffer(std::shared_ptr<Buffer> buf) : buf_{buf} {};
        ~PooledBuffer() {
            if (buf_) {
               BufferMemoryPool::instance().release(buf_);
            }
        }
        PooledBuffer(PooledBuffer &&other){
            buf_.swap(other.buf_);
        }
        PooledBuffer& operator=(PooledBuffer &&other){
            if(this != &other){
                if (buf_) {
                    BufferMemoryPool::instance().release(buf_);     // key!!!!
                }
                buf_ = std::move(other.buf_);   // should not swap
            }
            return *this;
        }
        std::shared_ptr<Buffer> get() const { return buf_; }

        std::shared_ptr<Buffer> operator->() const { return buf_; }     // key!!!!
        explicit operator bool() const { return buf_ != nullptr; }


    private:
        std::shared_ptr<Buffer> buf_;
    };
    friend class PooledBuffer;

    PooledBuffer acquire(size_t size) {
        // choose appropriate pool by size
        for(auto &pool : pools_){
            if(size <= pool->block_size()){
                return pool->allocate();
            }
        }
        // for huge sizes, allocate direct
        return std::make_shared<Buffer>(size);
    }

    void release(std::shared_ptr<Buffer> buffer){
        if (!buffer) return;
        size_t cap = buffer->capacity();
        for(auto &pool : pools_){
            if(cap == pool->block_size()){
                pool->deallocate(std::move(buffer));
                return;
            }
        }
    }

    void release(PooledBuffer &pooledBuffer) {
        auto buffer = pooledBuffer.get();
        if (!buffer) return;
        size_t cap = buffer->capacity();
        for(auto &pool : pools_){
            if(cap == pool->block_size()){
                pool->deallocate(std::move(buffer));
                return;
            }
        }

        // otherwise let shared_ptr free it
        // it is a problem some Buffer may be out of management
    }


    BufferMemoryPool() {
        pools_.emplace_back(std::make_unique<FixedSizePool>(kSmallSize, 100));
        pools_.emplace_back(std::make_unique<FixedSizePool>(kMediumSize, 100));
        pools_.emplace_back(std::make_unique<FixedSizePool>(kLargeSize, 50));
        pools_.emplace_back(std::make_unique<FixedSizePool>(kHugeSize, 10));
    }

    // for test use
    size_t free_count(int index){
        return pools_[index]->free_block_count();
    }

private:
    static constexpr size_t kSmallSize = 256;
    static constexpr size_t kMediumSize = 1024;
    static constexpr size_t kLargeSize = 8 * 1024;
    static constexpr size_t kHugeSize = 64 * 1024;

    std::vector<std::unique_ptr<FixedSizePool>> pools_;
};

// TODO: 
// weired
using PooledBuffer = BufferMemoryPool::PooledBuffer;