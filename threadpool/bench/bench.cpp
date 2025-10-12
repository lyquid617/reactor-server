#include <benchmark/benchmark.h>
#include <vector>
#include <algorithm>

static void BM_SortVector(benchmark::State& state) {
    // 在每个测试循环中，创建一个大小为state.range(0)的vector
    for (auto _ : state) {
        state.PauseTiming(); // 暂停计时，准备数据的时间不计入
        std::vector<int> v(state.range(0));
        std::generate(v.begin(), v.end(), std::rand);
        state.ResumeTiming(); // 恢复计时，只测量排序耗时
        
        // 这是我们要测量性能的核心操作
        std::sort(v.begin(), v.end());
    }
}
// 使用Range方法测试不同的输入大小：从2^8到2^14
BENCHMARK(BM_SortVector)->RangeMultiplier(2)->Range(1<<8, 1<<14);

// 生成main函数
BENCHMARK_MAIN();