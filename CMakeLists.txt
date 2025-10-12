cmake_minimum_required(VERSION 3.14)
project(MyProject)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)

# 添加主项目
add_subdirectory(src)

# 添加测试目录
enable_testing()
add_subdirectory(tests)