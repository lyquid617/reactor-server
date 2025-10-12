#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <memory>
#include <vector>
#include <chrono>

enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

class Logger {
public:
    Logger() : level(LogLevel::INFO) {}

    void setLevel(LogLevel newLevel) {
        level = newLevel;
    }

    void addOutput(std::ostream* out) {
        outputs.push_back(out);
    }

    void log(LogLevel msgLevel, const std::string& message, 
             const char* file, int line, const char* function) {
        if (msgLevel < level) return;

        std::ostringstream formatted;

        auto now = std::chrono::system_clock::now();
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
        
        std::tm local_time;
        localtime_r(&now_time_t, &local_time); 
        
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time);
        

        formatted << "[" << toString(msgLevel) << "] "
                  << "[" << buffer << "] "
                  << "[" << file << ":" << line << " " << function << "] "
                  << message << std::endl;

        std::lock_guard<std::mutex> lock(mutex);
        for (auto out : outputs) {
            *out << formatted.str();
            if (out == &std::cout || out == &std::cerr) {
                out->flush(); // 确保控制台输出立即显示
            }
        }
    }

private:
    std::string toString(LogLevel l) {
        switch (l) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO";
            case LogLevel::WARN:  return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    LogLevel level;
    std::mutex mutex;
    std::vector<std::ostream*> outputs;
};

// 全局Logger实例
Logger globalLogger;

// 日志宏，自动捕获文件、行号、函数名
#define LOG(level, message) \
    globalLogger.log(level, message, __FILE__, __LINE__, __func__)
