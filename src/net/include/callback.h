#pragma once

#include <functional>
#include <memory>
#include "tcpconnection.h"


class TcpConnection;
class Buffer;
class TimeStamp;

// like a member function capable of using member variable
using ConnectionCallback = std::function<void( std::shared_ptr<TcpConnection>) >;
using ReadDataCallback = std::function<void(std::shared_ptr<TcpConnection>, std::shared_ptr<Buffer>, TimeStamp)>;
using CloseCallback = std::function<void( std::shared_ptr<TcpConnection>) >;
using WriteCompleteCallback = std::function<void( std::shared_ptr<TcpConnection>) >;
using HighWatermarkCallback = std::function<void( std::shared_ptr<TcpConnection> , size_t ) >;