#pragma once

#include "platform.h"
#include <string>
#include <memory>
#include <chrono>
#include <functional>

class EventLoop;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(SOCKET_TYPE fd, EventLoop* loop);
    ~Connection();

    void onReadReady();
    void onWriteReady();

    void enqueueResponse(const std::string& data);
    
    SOCKET_TYPE getFd() const { return fd_; }
    void closeConnection();

    void updateLastActiveTime();
    bool isIdle(int timeoutSeconds) const;

    void runInLoop(std::function<void()> task);

    // Globals for routing
    static int server_argc;
    static char** server_argv;

private:
    void processRequest();

    SOCKET_TYPE fd_;
    EventLoop* loop_;
    std::string readBuffer_;
    std::string writeBuffer_;
    bool isClosing_;
    std::chrono::steady_clock::time_point lastActive_;
};
