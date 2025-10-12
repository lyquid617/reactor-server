#pragma once

#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>


class ThreadPool {
public:
    explicit ThreadPool(size_t threads)
        : stop(false){
        for(size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {       // worker loop , controlled by condition variable
                while(true) {
                    std::function<void()> task;{
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        /**
                         * worker thread wakeup condition
                         * - main thread call cond.notify()
                         * - where should cond.notify() be called ?
                         *  - the pool is stopping or there is a task throw into task_queue
                         */
                        this->condition.wait(lock, [this] { 
                            return this->stop || !this->tasks.empty(); 
                        });
                        // case 1. when stopping, worker thread do something ?
                        if(this->stop && this->tasks.empty())
                            return;
                        // case 2. retrieve a task from task queue, use move to avoid deep copy
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                        pending_tasks--;
                    }
                    // the lifetime of unique lock ended, free the lock and perform the task
                    task(); 
                }
            });
        }
    }
    ~ThreadPool(){
        shutdown();
    }

    void shutdown(){
        stop = true;
        // notify all worker threads for safety
        condition.notify_all();
        for(std::thread &worker : workers){
            if(worker.joinable()){
                // block the main thread, wait for all threads to end
                worker.join();
            }
        }
    }

    // must define in header
    #if __cplusplus >= 201703L
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future< std::invoke_result_t<F, Args...> >{
        using return_type = std::invoke_result_t<F, Args...>; // 现代写法

        // 1. 创建一个 packaged_task，包装任务和参数
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // 2. 获取与任务关联的 future
        std::future<return_type> res = task->get_future();

        // 3. 将任务封装后放入队列
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.emplace([task](){ (*task)(); }); // 将任务放入队列
        }
        condition.notify_one();
        return res; // 返回 future，用户可通过它异步获取结果
    }

    #else
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>{
        using return_type = typename std::result_of<F(Args...)>::type;
    
        // 1. warp f into packaged_task, to enqueue into task queue
        auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        // 2. get future to return to the caller, for it to get result
        std::future<return_type> res = task->get_future();
        // 3. lock the task_queue and enqueue
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            // wrap into lambda, to erase the type
            tasks.emplace([task](){ (*task)(); });
            pending_tasks++;
        }
        condition.notify_one(); // 通知一个等待中的工作线程
        return res;
    }
    #endif

private:
    std::vector<std::thread> workers;
    // use priority queue to support priority
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;     // as it's named, the mutex is for the task queue
    std::condition_variable condition;

    // status variable
    std::atomic<bool> stop{false};
    std::atomic<size_t> pending_tasks{0};
    // std::atomic<bool> stop_excess_{false};
    // std::atomic<int> task_id_counter{0};
    // std::atomic<size_t> active_threads{0};
    // std::atomic<size_t> completed_tasks{0};
    // std::atomic<size_t> total_threads{0};
    
};