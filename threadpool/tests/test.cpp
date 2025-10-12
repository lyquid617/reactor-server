#include <gtest/gtest.h>
#include <thread>
#include <future>
#include <vector>
#include <memory>
#include "threadpool.h" 

class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 在每个测试前创建线程池
        pool = std::make_unique<ThreadPool>(4); // 4个线程
    }
    
    void TearDown() override {
        // 在每个测试后关闭线程池
        pool->shutdown();
    }
    
    std::unique_ptr<ThreadPool> pool;
};

double test_mul(int a, double b){
    return a * b;
}

class Calculator{
public:
    int add(int a, int b){
        return a + b;
    }
};

// 测试线程池能否正常执行简单任务
TEST_F(ThreadPoolTest, ShouldExecuteSimpleTask) {
    std::promise<void> promise;
    auto future = promise.get_future();
    
    // leave the promise to perform async
    pool->enqueue([&promise]() {
        promise.set_value();
    });
    
    // 等待任务完成，设置超时防止死锁
    auto status = future.wait_for(std::chrono::seconds(1));
    ASSERT_EQ(status, std::future_status::ready);
}

// 测试线程池能返回正确的结果
TEST_F(ThreadPoolTest, TestPerfectForwarding) {
    auto future1 = pool->enqueue([](int a, int b) {
        return a + b;
    }, 10, 20);
    
    auto future2 = pool->enqueue(test_mul, 3, 3.14);

    Calculator calc;
    auto future3 = pool->enqueue(&Calculator::add, &calc, 1, 2);


    ASSERT_EQ(future1.get(), 30);
    ASSERT_EQ(future2.get(), 9.42);
    ASSERT_EQ(future3.get(), 3);
}

// 测试异常传播
TEST_F(ThreadPoolTest, ShouldPropagateExceptions) {
    auto future = pool->enqueue([]() {
        throw std::runtime_error("Test exception");
        return 0;
    });
    
    ASSERT_THROW(future.get(), std::runtime_error);
}