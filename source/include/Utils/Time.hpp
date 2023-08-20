#pragma once

#include "Utils/Format.hpp"

#include <chrono>
#include <string>

#ifdef __APPLE__
#include <ctime>
#include <fcntl.h>
#include <sys/time.h>
#endif

MAA_NS_BEGIN

inline std::string format_now()
{
    timeval tv = {};
    gettimeofday(&tv, nullptr);
    time_t nowtime = tv.tv_sec;
    tm* tm_info = localtime(&nowtime);
    return MAA_FMT::format("{:0>4}-{:0>2}-{:0>2} {:0>2}:{:0>2}:{:0>2}.{:0>3}", tm_info->tm_year + 1900, tm_info->tm_mon,
                       tm_info->tm_mday, tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, tv.tv_usec / 1000);
}

inline std::string now_filestem()
{
    timeval tv = {};
    gettimeofday(&tv, nullptr);
    time_t nowtime = tv.tv_sec;
    tm* tm_info = localtime(&nowtime);
    return MAA_FMT::format("{:0>4}.{:0>2}.{:0>2}-{:0>2}.{:0>2}.{:0>2}.{}", tm_info->tm_year + 1900, tm_info->tm_mon,
                       tm_info->tm_mday, tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, tv.tv_usec);
}

inline std::chrono::milliseconds duration_since(const std::chrono::steady_clock::time_point& start_time)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
}

MAA_NS_END
