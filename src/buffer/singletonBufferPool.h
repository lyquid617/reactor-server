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

namespace buffer_internal {
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

}

class FixedSizePool : Noncopyable {
using Buffer = buffer_internal::Buffer;
public:
    // manage shared_ptr<Buffer> with fixed capacity
    FixedSizePool(size_t block_size, size_t prealloc_count = 100)
        : block_size_(block_size) {
        preallocate(prealloc_count);
    }

    ~FixedSizePool() = default;

    // allocate returns a Buffer* from pool or creates a new one
    Buffer* allocate() {
        // lock for stack: free_list
        std::lock_guard<std::mutex> lock(mutex_);
        if (free_list_.empty()) {
            // create new Buffer objects to expand
            for (size_t i = 0; i < expand_size_; ++i) {
                storage_.push_back(std::make_unique<Buffer>(block_size_));
                free_list_.push( storage_.back().get() );
            }
            expand_size_ = std::min(expand_size_ * 2, MAX_EXPAND_SIZE);
        }
        if (free_list_.empty()) return nullptr;
        auto buf = free_list_.top();
        free_list_.pop();
        return buf;
    }

    // return buffer to pool only if capacity matches
    void deallocate(Buffer* buf) {
        if (!buf) return;
        if (buf->capacity() != block_size_) return; // ignore mismatched sizes
        std::lock_guard<std::mutex> lock(mutex_);
        // reset buffer pointers before returning
        buf->retrieveAll();
        free_list_.push(buf);
    }

    size_t block_size() const { return block_size_; }
    size_t free_block_count() const { return free_list_.size(); }

private:
    void preallocate(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            storage_.push_back(std::make_unique<Buffer>(block_size_));
            free_list_.push(storage_.back().get());
        }
    }

    size_t block_size_;
    size_t expand_size_ = 10;
    std::vector<std::unique_ptr<Buffer>> storage_;
    // lightweight raw pointer
    std::stack< Buffer* > free_list_;       
    std::mutex mutex_;

    static constexpr size_t MAX_EXPAND_SIZE = 1000;
};

// singleton instance
class BufferMemoryPool : Noncopyable {
using Buffer = buffer_internal::Buffer;
public:
    static BufferMemoryPool& instance() {
        static BufferMemoryPool pool;
        return pool;
    }
    // move-only RAII handle
    class PooledBuffer : Noncopyable{
    public:
        PooledBuffer() = default;
        PooledBuffer(Buffer *buf, BufferMemoryPool* pool, int bucket) 
            : buf_{buf} 
            , pool_{pool}
            , bucket_idx_{bucket} {};
        ~PooledBuffer() {
            if (buf_ && pool_) {
                pool_->releaseRaw(buf_, bucket_idx_);
            }
        }
        PooledBuffer(PooledBuffer &&other) noexcept
        :buf_(other.buf_), pool_(other.pool_), bucket_idx_(other.bucket_idx_){
            other.buf_ = nullptr;
            other.pool_ = nullptr;
            other.bucket_idx_ = -1;
        }
        PooledBuffer& operator=(PooledBuffer &&other){
            if(this != &other){
                if (buf_ && pool_) {
                    pool_->releaseRaw(buf_, bucket_idx_);     // key!!!!
                }
                buf_ = other.buf_;
                pool_ = other.pool_;
                bucket_idx_ = other.bucket_idx_;
                other.buf_ = nullptr;
                other.pool_ = nullptr;
                other.bucket_idx_ = -1;
            }
            return *this;
        }
        Buffer* get() const { return buf_; }

        Buffer* operator->() const { return buf_; }     // key!!!!
        explicit operator bool() const { return buf_ != nullptr; }

        // destruct object but keep the raw
        Buffer* detach() {
            Buffer* tmp = buf_;
            buf_ = nullptr;
            pool_ = nullptr;
            bucket_idx_ = -1;
            return tmp;
        }

    private:
        Buffer *buf_ = nullptr;
        BufferMemoryPool *pool_ = nullptr;
        int bucket_idx_ = -1;
    };
    friend class PooledBuffer;

    // Acquire a move-only PooledBuffer ( so dont need to care about competition )
    PooledBuffer acquire(size_t size) {
        for(int i = 0; i < pools_.size(); ++i){
            auto &pool = pools_[i];
            if(size <= pool->block_size()){
                Buffer*raw = pool->allocate();
                if(!raw){
                    return PooledBuffer(nullptr, nullptr, -1);
                }
                return PooledBuffer(raw, this, i);
            }
        }
        // for huge, allocate directly, delete on release
        Buffer *raw = new Buffer(size);
        return PooledBuffer(raw, this, -1);
    }

    // when manually releasing, you should do explicitly what destructor do implicitly
    void release(PooledBuffer &pooledBuffer) {
        if (!pooledBuffer) return;
        Buffer* buf = pooledBuffer.detach();
        releaseRaw(buf, buf ? getBucketIndexForCapacity(buf->capacity()) : -1);
    }


    
    // for test use
    size_t free_count(int index){
        if(index < 0 || static_cast<size_t>(index) >= pools_.size()) return 0;
        return pools_[index]->free_block_count();
    }
    
private:
    BufferMemoryPool() {
        pools_.emplace_back(std::make_unique<FixedSizePool>(kSmallSize, 100));
        pools_.emplace_back(std::make_unique<FixedSizePool>(kMediumSize, 100));
        pools_.emplace_back(std::make_unique<FixedSizePool>(kLargeSize, 50));
        pools_.emplace_back(std::make_unique<FixedSizePool>(kHugeSize, 10));
    }
    void releaseRaw(Buffer *buffer, int bucket_idx){
        if(!buffer) return;
        if(bucket_idx >= 0 && bucket_idx < pools_.size()){
            pools_[bucket_idx]->deallocate(buffer);
            return;
        }
        // unmanaged large buffer
        delete buffer;
    }
    // helper: find bucket index by exact capacity (used by release)
    int getBucketIndexForCapacity(size_t cap) const {
        for (size_t i = 0; i < pools_.size(); ++i) {
            if (pools_[i]->block_size() == cap) return static_cast<int>(i);
        }
        return -1;
    }
    static constexpr size_t kSmallSize = 256;
    static constexpr size_t kMediumSize = 1024;
    static constexpr size_t kLargeSize = 8 * 1024;
    static constexpr size_t kHugeSize = 64 * 1024;

    std::vector<std::unique_ptr<FixedSizePool>> pools_;
};

using PooledBuffer = BufferMemoryPool::PooledBuffer;