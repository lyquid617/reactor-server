#include <iostream>
#include <sstream>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <atomic>
#include <filesystem>

#include "noncopyable.h"
#include "timestamp.h"

namespace{
    std::string get_filename(const std::string& full_path) {
        return std::filesystem::path(full_path).filename().string();
    }
}

enum class LogLevel  { DEBUG, INFO, WARN, ERROR, FATAL };

/** singleton logger 
 * TODO: add compile option support
*/
class Logger  : Noncopyable {
public:

    class LogStream{    // lifetime within a single log
    public:
        LogStream(LogLevel msgLevel, const char* file, int line, const char* function, Logger &logger) 
        : enabled_(msgLevel >= logger.get_level() ), logger_(logger) {
            if(enabled_){
                logger_.write_header(msgLevel, file, line, function);
            }
        }

        ~LogStream() {  // nextline when flush
            if(enabled_){
                // logger_.write_message(stream_.str());
                logger_.write_footer();
            }
        }
        
        template<typename T>
        LogStream& operator<<(const T& value) {
            if (enabled_) {
                // stream_ << value;
                logger_.write_content(value);
            }
            return *this;
        }
        
    private:
        bool enabled_;
        std::ostringstream stream_;
        Logger &logger_;
    };

    static Logger& instance() {
        static Logger logger;  
        return logger;
    }

    void setLevel(LogLevel newLevel) {
        level_.store(newLevel, std::memory_order_release);
    }

    void set_output(std::ostream& output) {
        std::lock_guard lock(mutex_);
        output_ = &output;
    }

    LogLevel get_level(){
        return level_.load(std::memory_order_acquire);
    }

    // printf-style formatting support
    void logf(LogLevel msgLevel, const char* file, int line, const char* function, const char* fmt, ...) {
        if (msgLevel < level_) return;

        // format into a string using va_list
        va_list args;
        va_start(args, fmt);
        // compute required size
        va_list args_copy;
        va_copy(args_copy, args);
        int needed = vsnprintf(nullptr, 0, fmt, args_copy);
        va_end(args_copy);

        std::string msg;
        if (needed >= 0) {
            msg.resize(needed + 1);
            vsnprintf(&msg[0], msg.size(), fmt, args);
            // remove trailing null
            if (!msg.empty() && msg.back() == '\0') msg.pop_back();
        } else {
            // fallback: put empty message on format error
            msg = "(logger format error)";
        }
        va_end(args);

        write_header(msgLevel, file, line, function);
        write_message(msg);
    }
    
private:
    Logger() {
        setLevel(LogLevel::INFO);
        output_ = &std::cout;
    }

    void write_header(LogLevel level, const char* file, int line, const char* function) {
        std::lock_guard lock(mutex_);
        if (output_ && level >= level_) {
            *output_ << "[" << TimeStamp::now().toFormattedString() << "] "
                     << "[" << toString(level) << "] "
                     << "[" << get_filename(file) << ":" << line << " " << function <<  "() ] ";
        }
    }

    void write_message(const std::string &msg){
        std::lock_guard lock(mutex_);
        if (output_ && !msg.empty()) {
            *output_ << msg << std::endl;
        }
    }
    
    template<typename T>
    void write_content(const T &t){
        std::lock_guard lock(mutex_);
        if (output_ ) {
            *output_ << t;
        } 
    }
    
    void write_footer(){
        std::lock_guard lock(mutex_);
        *output_ << std::endl;
    }


    std::string toString(LogLevel l) {
        static const char* levels[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
        return levels[static_cast<int>(l)];
    }


    std::atomic<LogLevel> level_;
    std::mutex mutex_;
    // cout by default
    std::ostream* output_;

};


// stream style logging
#define LOG(level) \
    Logger::LogStream(level, __FILE__, __LINE__, __func__, Logger::instance())

#define LOG_DEBUG LOG(LogLevel::DEBUG)
#define LOG_INFO  LOG(LogLevel::INFO)
#define LOG_WARN  LOG(LogLevel::WARN)
#define LOG_ERROR LOG(LogLevel::ERROR)
#define LOG_FATAL LOG(LogLevel::FATAL)


// printf style logging
#define LOGF(level, fmt, ...) \
    Logger::instance().logf(level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LOGF_DEBUG(fmt, ...) LOGF(LogLevel::DEBUG, fmt, ##__VA_ARGS__)
#define LOGF_INFO(fmt, ...)  LOGF(LogLevel::INFO,  fmt, ##__VA_ARGS__)
#define LOGF_WARN(fmt, ...)  LOGF(LogLevel::WARN, fmt, ##__VA_ARGS__)
#define LOGF_ERROR(fmt, ...) LOGF(LogLevel::ERROR, fmt, ##__VA_ARGS__)
#define LOGF_FATAL(fmt, ...) LOGF(LogLevel::FATAL, fmt, ##__VA_ARGS__)

