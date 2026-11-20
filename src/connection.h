#pragma once

#include "platform.h"
#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

class EventLoop;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(SOCKET_TYPE fd, EventLoop* loop);
    ~Connection();

    void onReadReady();
    void onWriteReady();
    void enqueueResponse(const std::string& data);
    void streamFile(std::string path, bool allowDefaultFallback);
    void closeConnection();
    bool isIdle(int timeoutSeconds) const;
    void runInLoop(std::function<void()> task);

    static int server_argc;
    static char** server_argv;

private:
    void processRequest();
    void updateInterest();
    void disconnect();
    void requestStreamRead();
    void finishWrite();

    SOCKET_TYPE fd_;
    EventLoop* loop_;
    std::string readBuffer_;
    std::string writeBuffer_;
    bool responsePending_ = false;
    bool closeAfterResponse_ = false;
    std::shared_ptr<std::ifstream> stream_;
    size_t streamPeakBufferedBytes_ = 0;
    bool reportStreamPeak_ = false;
    bool streamReadPending_ = false;
    std::chrono::steady_clock::time_point lastActive_;
};
