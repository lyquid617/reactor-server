#include "eventloop.h"
#include "logger.h"
#include <iostream>
#include <unistd.h>
#include <sys/eventfd.h>

// prevent from creating more than one loop on a single thread
thread_local EventLoop* t_loopInThisThread = nullptr;

static int createWakeupFd(){
    int evfd = eventfd(0, EFD_NONBLOCK | EFD_NONBLOCK);
    if(evfd < 0){
        LOG_ERROR << "Wakeup fd creation failed";
    }else{
        LOG_DEBUG << "Create a new wakeup fd : " << evfd;
    }
    return evfd;
}

void EventLoop::updateEpoller(int operation, Channel* ch){
    int fd = ch->fd();
    epoll_event event{};        // auto init to zero with {}
    event.events = ch->events();
    event.data.ptr = ch;
    if(epoll_ctl( epollFd_, operation, fd, &event) < 0){
        LOG_ERROR << "epoll_ctl failed on eventloop" << this;
    }
}

EventLoop::EventLoop() 
    : looping_(false) 
    , stop_(false)
    , doingPendingFunctors_(false)
    , epollFd_(epoll_create1(EPOLL_CLOEXEC))
    , eventList_(kEventListSize)
    , threadId_(CurrentThread::tid())
    , wakeupFd_(createWakeupFd()) 
    , wakeupChannel_(new Channel(this, wakeupFd_)) {
    LOG_DEBUG << "Create a new eventloop on thread " << threadId_;
    if(t_loopInThisThread){
        LOG_FATAL << "Another eventloop" << t_loopInThisThread << "already created on thread" << threadId_;
    }else{
        t_loopInThisThread = this;
    }
    if (epollFd_ == -1) {
        LOG_ERROR << "epoll_create1() failed";
    }else{
        LOG_DEBUG << "create a new epoll fd " << epollFd_ << " on thread" << threadId_;
    }
    // write something to eventfd to wakeup this eventloop
    wakeupChannel_->setReadCallBack( [this] () { handleWakeup(); } );
    wakeupChannel_->enableReading();

}

EventLoop::~EventLoop(){
    ::close(epollFd_);

    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);

    t_loopInThisThread = nullptr;
}

void EventLoop::updateChannel(Channel *ch){
    auto state = ch->state();
    if(state  == ChannelState::INIT){
        // add a new conn fd to poller
        int fd = ch->fd();
        channels_[fd] = ch;

        updateEpoller(EPOLL_CTL_ADD, ch);
        ch->setState(ChannelState::POLLING);
    }else if(state == ChannelState::POLLING){
        if(ch->isNonEvent()){
            // remove the polling fd from epoll fd set
            updateEpoller(EPOLL_CTL_DEL, ch);
            ch->setState(ChannelState::REMOVED);
        }else{
            // just mod the events
            updateEpoller(EPOLL_CTL_MOD, ch);
        }
    }else if(state == ChannelState::REMOVED){
        // add back to fd set
        // check if is removed from the channel map
        int fd = ch->fd();
        if( channels_.find(fd) == channels_.end() || channels_[fd] != ch ){
            // the original channel is removed
            LOG_ERROR << "Try updating a removed channel";
        }else{
            // add back to epoller
            updateEpoller(EPOLL_CTL_ADD, ch);
            ch->setState(ChannelState::POLLING);
        }
    }
}
// the state is kinda chaos
void EventLoop::removeChannel(Channel *ch){
    channels_.erase(ch->fd());
    if(ch->state() == ChannelState::POLLING){
        updateEpoller(EPOLL_CTL_DEL, ch);
    }
    ch->setState(ChannelState::REMOVED);
}


bool EventLoop::hasChannel(Channel *ch){
    return channels_.find(ch->fd()) != channels_.end() && channels_[ch->fd()] == ch;
}

void EventLoop::wakeup(){
    uint64_t one = 1;
    auto n = write(wakeupFd_, &one, sizeof(one));
    if(n != sizeof(one)){
        LOG_ERROR << "Writing wakeup fd more than 8 bytes";
    }
}

void EventLoop::handleWakeup(){
    uint64_t buf{};
    auto n = read(wakeupFd_, &buf, sizeof(buf));
    if(buf != 1 || n != sizeof(buf)){
        LOG_ERROR << "Wakeup fd polluted";
    }
}


void EventLoop::run(){
    looping_ = true;
    stop_ = false;
    // reuse eventList buffer while supporting dynamic extention
    while (!stop_) {
        activeChannels_.clear();
        int n = epoll_wait(epollFd_, eventList_.data() , static_cast<int>(eventList_.size()), 100); // 100ms超时
        lastEpollTime_ = TimeStamp::now();
        if (n == -1) {
            if (errno == EINTR) continue;
            LOG_ERROR << "epoll_wait() failed";
        }
        if (n == eventList_.size()) {   // manualy resize since we use it as static array
            eventList_.resize(eventList_.size() * 2);
        }
        for (int i = 0; i < n; ++i) {
            Channel *channel = static_cast<Channel*>(eventList_[i].data.ptr);
            channel->setRevents(eventList_[i].events);
            activeChannels_.push_back(channel);
        }
        // a middle layer can support priority, filter... in the future
        for (Channel *channel : activeChannels_){
            channel->handleEvent(lastEpollTime_);
        }

        doPendingFunctors();
    }
    looping_ = false;
}

void EventLoop::stop() {
    stop_ = true;
    // wakeup blocking epoll_wait() so can destruct inmediately
    if(!isInLoopThread()) wakeup();
    // current thread calling stop means not blocked
}

void EventLoop::doPendingFunctors(){
    std::vector<Functor> functors;
    doingPendingFunctors_ = true;
    {
        std::unique_lock<std::mutex> lock{mutex_};
        functors.swap(pendingFunctors_);  // use cache to make fine-grain lock
    }   
    for(const Functor &functor : functors){
        functor();
    }
    doingPendingFunctors_ = false;
}