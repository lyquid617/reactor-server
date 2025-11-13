#include "tcpconnection.h"
#include <unistd.h>
#include <iostream>


// create a channel, set callbacks for it 
TcpConnection::TcpConnection(
                    int fd, 
                    EventLoop* loop, 
                    const std::string &name, 
                    const InetAddress &localAddr, 
                    const InetAddress &clientAddr) 
    : loop_(loop)
    , socket_(std::make_unique<Socket>(fd))
    , channel_(std::make_unique<Channel>(loop, fd))
    , name_(name)
    , localAddr_(localAddr_)
    , clientAddr_(clientAddr)
    , state_(State::CONNECTING)
    , highWaterMark_(64 * 1024 *1024){
        channel_->setReadCallBack( [this](TimeStamp ts){ handleRead(ts); } );
        channel_->setWriteCallBack( [this](){ handleWrite(); } );
        channel_->setCloseCallBack( [this](){ handleClose(); } );
        channel_->setErrorCallBack( [this](){ handleError(); } );
        LOG_INFO << "TCP Connction " << name_.c_str() << " with " << clientAddr_.toIp() << " created at fd " << socket_->fd();
        socket_->setKeepAlive(true);

}

TcpConnection::~TcpConnection(){
    // socketfd closed in ~Socket()
    LOG_INFO << "TCP Connection " << name_.c_str() << " with " << clientAddr_.toIp() << " closed fd " << socket_->fd();
}

// the structure is fit for extending functions
void TcpConnection::establishConnection(){
    channel_->tie(shared_from_this());  // weak_ptr observer
    channel_->enableReading();      // read from client
    
    setState(State::CONNECTED);
    connectionCallback_(shared_from_this());
}

// for Tcpserver ( local end ) to close the connection
void TcpConnection::destroyConnection(){
    if(state_ == State::CONNECTED){
        setState(State::DISCONNECTED);
        channel_->disableAll();
        connectionCallback_(shared_from_this());
    }
    channel_->remove();
}

void TcpConnection::handleRead(TimeStamp ts){
    

}