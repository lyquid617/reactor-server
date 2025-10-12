#include <cstddef>
#include <cstdlib>
#include <memory>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <cassert>
#include <new> // std::bad_alloc

class MemoryManager {
public:
    // 内存分配策略
    enum class AllocationPolicy {
        Standard,       // 使用标准 malloc/free
        Pooled,         // 使用内存池
        Hybrid          // 小对象用池，大对象直接分配
    };

    // 配置选项
    struct Config {
        AllocationPolicy policy = AllocationPolicy::Hybrid;
        size_t max_pool_size = 1024 * 1024 * 100; // 100MB
        size_t small_object_threshold = 4096;     // 4KB
        bool enable_statistics = true;
        bool enable_thread_local = true;
    };

    // 获取单例实例
    static MemoryManager& instance() {
        static MemoryManager manager;
        return manager;
    }

    // 配置内存管理器（必须在任何分配前调用）
    // 全局配置需要加锁
    void configure(const Config& config) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_ = config;
    }

    // 内存分配函数
    void* allocate(size_t size) {
        if (size == 0) return nullptr;
        
        // 更新统计信息
        if (config_.enable_statistics) {
            update_statistics(size, true);
        }

        // 根据策略选择分配方式
        switch (config_.policy) {
            case AllocationPolicy::Standard:
                return standard_allocate(size);
            case AllocationPolicy::Pooled:
                return pooled_allocate(size);
            case AllocationPolicy::Hybrid:
                return size <= config_.small_object_threshold ? 
                    pooled_allocate(size) : standard_allocate(size);
            default:
                return standard_allocate(size);
        }
    }

    // 内存释放函数
    void deallocate(void* ptr, size_t size) {
        if (!ptr) return;
        
        // 更新统计信息
        if (config_.enable_statistics) {
            update_statistics(size, false);
        }

        // 根据策略选择释放方式
        switch (config_.policy) {
            case AllocationPolicy::Standard:
                standard_deallocate(ptr);
                break;
            case AllocationPolicy::Pooled:
                pooled_deallocate(ptr, size);
                break;
            case AllocationPolicy::Hybrid:
                if (size <= config_.small_object_threshold) {
                    pooled_deallocate(ptr, size);
                } else {
                    standard_deallocate(ptr);
                }
                break;
            default:
                standard_deallocate(ptr);
        }
    }

    // 统计信息结构
    struct Statistics {
        size_t total_allocated = 0;
        size_t total_freed = 0;
        size_t current_usage = 0;
        size_t peak_usage = 0;
        size_t allocation_count = 0;
        size_t deallocation_count = 0;
        size_t pool_hits = 0;
        size_t pool_misses = 0;
    };

    // 获取统计信息
    Statistics get_statistics() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return global_stats_;
    }

    // 重置统计信息
    void reset_statistics() {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        global_stats_ = Statistics{};
    }

    // 内存跟踪（调试用）
    void enable_memory_tracking(bool enable) {
        std::lock_guard<std::mutex> lock(tracking_mutex_);
        tracking_enabled_ = enable;
    }

    // 内存泄漏检测
    void detect_leaks() {
        std::lock_guard<std::mutex> lock(tracking_mutex_);
        if (!tracking_enabled_) {
            std::cerr << "Memory tracking not enabled\n";
            return;
        }

        if (live_allocations_.empty()) {
            std::cout << "No memory leaks detected\n";
            return;
        }

        std::cerr << "Memory leaks detected:\n";
        for (const auto& [ptr, info] : live_allocations_) {
            std::cerr << "  Leak at " << ptr 
                      << " size " << info.size 
                      << " allocated by " << info.source 
                      << ":" << info.line << "\n";
        }
    }

    // 内存分配跟踪信息
    struct AllocationInfo {
        size_t size;
        const char* source;
        int line;
    };

    // 带调试信息的分配函数
    void* tracked_allocate(size_t size, const char* source, int line) {
        void* ptr = allocate(size);
        if (ptr && tracking_enabled_) {
            std::lock_guard<std::mutex> lock(tracking_mutex_);
            live_allocations_[ptr] = {size, source, line};
        }
        return ptr;
    }

    // 带调试信息的释放函数
    void tracked_deallocate(void* ptr, size_t size, const char* source, int line) {
        if (ptr && tracking_enabled_) {
            std::lock_guard<std::mutex> lock(tracking_mutex_);
            auto it = live_allocations_.find(ptr);
            if (it != live_allocations_.end()) {
                live_allocations_.erase(it);
            } else {
                std::cerr << "Double free or invalid pointer at " 
                          << source << ":" << line << "\n";
            }
        }
        deallocate(ptr, size);
    }

private:
    // 私有构造函数（单例模式）
    MemoryManager() {
        // 默认配置
        config_.policy = AllocationPolicy::Hybrid;
        config_.max_pool_size = 1024 * 1024 * 100; // 100MB
        config_.small_object_threshold = 4096;      // 4KB
        config_.enable_statistics = true;
        config_.enable_thread_local = true;
    }

    // 禁止复制
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    // 标准分配
    void* standard_allocate(size_t size) {
        void* ptr = std::malloc(size);
        if (!ptr) {
            ptr = handle_out_of_memory(size);
        }
        return ptr;
    }

    // 标准释放
    void standard_deallocate(void* ptr) {
        std::free(ptr);
    }

    // 内存池分配
    void* pooled_allocate(size_t size) {
        // 获取线程本地内存池
        auto& pool = get_thread_pool();
        
        // 尝试从池中分配
        if (void* ptr = pool.allocate(size)) {
            if (config_.enable_statistics) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                global_stats_.pool_hits++;
            }
            return ptr;
        }
        
        // 池分配失败，回退到标准分配
        if (config_.enable_statistics) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            global_stats_.pool_misses++;
        }
        return standard_allocate(size);
    }

    // 内存池释放
    void pooled_deallocate(void* ptr, size_t size) {
        // 获取线程本地内存池
        auto& pool = get_thread_pool();
        
        // 尝试放回池中
        if (!pool.deallocate(ptr, size)) {
            // 池拒绝回收，使用标准释放
            standard_deallocate(ptr);
        }
    }

    // 内存不足处理
    void* handle_out_of_memory(size_t size) {
        // 尝试释放预留内存
        if (try_release_reserved_memory()) {
            // 重试分配
            void* ptr = std::malloc(size);
            if (ptr) return ptr;
        }
        
        // 调用用户定义的处理函数
        if (oom_handler_) {
            oom_handler_(size);
        }
        
        // 抛出标准异常
        throw std::bad_alloc();
    }

    // 尝试释放预留内存
    bool try_release_reserved_memory() {
        // 这里可以添加释放预留内存的逻辑
        // 例如：释放缓存、压缩内存池等
        return false;
    }

    // 更新统计信息
    void update_statistics(size_t size, bool is_alloc) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        
        if (is_alloc) {
            global_stats_.total_allocated += size;
            global_stats_.current_usage += size;
            global_stats_.allocation_count++;
            
            if (global_stats_.current_usage > global_stats_.peak_usage) {
                global_stats_.peak_usage = global_stats_.current_usage;
            }
        } else {
            global_stats_.total_freed += size;
            global_stats_.current_usage -= size;
            global_stats_.deallocation_count++;
        }
    }

    // 内存池类（线程本地）
    class ThreadMemoryPool {
    public:
        // 分配内存
        void* allocate(size_t size) {
            // 简化实现：实际应用中应使用更复杂的内存池结构
            if (size > block_size_) {
                return nullptr; // 太大，不适合池
            }
            
            if (free_list_.empty()) {
                if (!allocate_new_block()) {
                    return nullptr;
                }
            }
            
            void* ptr = free_list_.back();
            free_list_.pop_back();
            return ptr;
        }
        
        // 释放内存
        bool deallocate(void* ptr, size_t size) {
            if (size > block_size_) {
                return false; // 不是从这个池分配的
            }
            
            free_list_.push_back(ptr);
            return true;
        }
        
    private:
        // 分配新内存块
        bool allocate_new_block() {
            void* block = std::malloc(block_size_ * block_count_);
            if (!block) return false;
            
            // 将新块分割成小块
            char* current = static_cast<char*>(block);
            for (size_t i = 0; i < block_count_; ++i) {
                free_list_.push_back(current);
                current += block_size_;
            }
            
            blocks_.push_back(block);
            return true;
        }
        
        static constexpr size_t block_size_ = 256; // 每个块大小
        static constexpr size_t block_count_ = 1024; // 每块中的分配单元数
        
        std::vector<void*> free_list_;
        std::vector<void*> blocks_;
    };

    // 获取线程本地内存池
    ThreadMemoryPool& get_thread_pool() {
        if (config_.enable_thread_local) {
            // 使用线程本地存储
            thread_local ThreadMemoryPool pool;
            return pool;
        } else {
            // 使用全局池（需要加锁）
            std::lock_guard<std::mutex> lock(pool_mutex_);
            static ThreadMemoryPool global_pool;
            return global_pool;
        }
    }

    // 内存不足处理函数类型
    using OOMHandler = void(*)(size_t);
    
    // 设置内存不足处理函数
    void set_oom_handler(OOMHandler handler) {
        oom_handler_ = handler;
    }

    // 成员变量
    Config config_;
    mutable std::mutex config_mutex_;
    mutable std::mutex stats_mutex_;
    mutable std::mutex pool_mutex_;
    mutable std::mutex tracking_mutex_;
    
    Statistics global_stats_;
    bool tracking_enabled_ = false;
    std::unordered_map<void*, AllocationInfo> live_allocations_;
    OOMHandler oom_handler_ = nullptr;
};

// 全局重载 new/delete 运算符
void* operator new(size_t size) {
    return MemoryManager::instance().allocate(size);
}

void operator delete(void* ptr) noexcept {
    MemoryManager::instance().deallocate(ptr, 0);
}

void operator delete(void* ptr, size_t size) noexcept {
    MemoryManager::instance().deallocate(ptr, size);
}

// 带调试信息的 new/delete
void* operator new(size_t size, const char* file, int line) {
    return MemoryManager::instance().tracked_allocate(size, file, line);
}

void operator delete(void* ptr, const char* file, int line) noexcept {
    MemoryManager::instance().tracked_deallocate(ptr, 0, file, line);
}

// 宏简化调试分配
#ifdef MEMORY_DEBUG
#define new new(__FILE__, __LINE__)
#endif
