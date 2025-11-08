#pragma once

#include "noncopyable.h"

#include "logger.h"

#include <functional>
#include <memory>

/**
 * each channel owns a fd to monitor, 
 * manage the events concerned and callback
 * It is like an I/O channel, with I/O events on fd
 */

class Channel : Noncopyable{   // exclusive ownership for resources
public:
    using EventCallBack = std::function<void()>;


};