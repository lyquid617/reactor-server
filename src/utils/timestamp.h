#pragma once


#include <iostream>
#include <chrono>
#include <string>
#include <array>

using namespace std::chrono;
class TimeStamp{
public: 
    TimeStamp() : microSecondsSinceEpoch_(0) {}
    explicit TimeStamp(int64_t microSecondsSinceEpoch)
        : microSecondsSinceEpoch_(microSecondsSinceEpoch) {}

    explicit TimeStamp(const system_clock::time_point& tp)
        : microSecondsSinceEpoch_(duration_cast<microseconds>(
            tp.time_since_epoch())) {}

    static TimeStamp now(){
        return TimeStamp(system_clock::now());
    }
    
    // like [2025/11/08 18:03:13.295632]
    std::string toFormattedString(bool showMicroseconds = true) const{
        auto tp = system_clock::time_point(
            duration_cast<system_clock::duration>(microSecondsSinceEpoch_));
        std::time_t time = system_clock::to_time_t(tp);
        tm tm_time;
        localtime_r(&time, &tm_time);   // thread-safe transfer
        int64_t microseconds = microSecondsSinceEpoch_.count() % kmicroSecondsPerSecond;
        std::array<char, 64> buf;   // buffer on stack with accessible size
        if(showMicroseconds){
            snprintf(buf.data(), buf.size(),  "%4d/%02d/%02d %02d:%02d:%02d.%06ld",
                tm_time.tm_year + 1900,
                tm_time.tm_mon + 1,
                tm_time.tm_mday,
                tm_time.tm_hour,
                tm_time.tm_min,
                tm_time.tm_sec,
                microseconds    
            );
        }else{
            snprintf(buf.data(), buf.size(),  "%4d/%02d/%02d %02d:%02d:%02d",
                tm_time.tm_year + 1900,
                tm_time.tm_mon + 1,
                tm_time.tm_mday,
                tm_time.tm_hour,
                tm_time.tm_min,
                tm_time.tm_sec
            );
        }
        return buf.data();
    }

    int64_t microSecondsSinceEpoch() const { return microSecondsSinceEpoch_.count(); }

    int64_t secondsSinceEpoch() const { 
        return duration_cast<seconds>(microSecondsSinceEpoch_).count(); 
    }

    TimeStamp& addMicroseconds(int64_t microsec) {
        microSecondsSinceEpoch_ += std::chrono::microseconds(microsec);
        return *this;
    }

    bool valid() const { return microSecondsSinceEpoch() != 0; }

    private:
    microseconds microSecondsSinceEpoch_;
    static const int kmicroSecondsPerSecond = 1000000;

};

inline bool operator<(TimeStamp lhs, TimeStamp rhs)
{
    return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
}

inline bool operator==(TimeStamp lhs, TimeStamp rhs)
{
    return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
}