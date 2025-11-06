cmake_minimum_required(VERSION 3.14)
project(hpcserver
DESCRIPTION "web server implemented by modern cpp"
LANGUAGES "CXX"
)


set(CMAKE_CXX_STANDARD 17)


add_subdirectory(buffer)
add_subdirectory(logger)
add_subdirectory(net)
add_subdirectory(threadpool)
add_subdirectory(timer)
