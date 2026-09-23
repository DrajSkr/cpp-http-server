#include "connection.h"
#include "event_loop.h"
#include "request_handler.h"
// #include <iostream>

int Connection::server_argc = 0;
char** Connection::server_argv = nullptr;

Connection::Connection(SOCKET_TYPE fd, EventLoop* loop) 
    : fd_(fd), loop_(loop), isClosing_(false) {
    updateLastActiveTime();
}

Connection::~Connection() {
    closeConnection();
}

void Connection::updateLastActiveTime() {
    lastActive_ = std::chrono::steady_clock::now();
}

void Connection::runInLoop(std::function<void()> task) {
    loop_->runInLoop(std::move(task));
}

bool Connection::isIdle(int timeoutSeconds) const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - lastActive_).count() > timeoutSeconds;
}

void Connection::closeConnection() {
    if (fd_ != INVALID_SOCK) {
        closeSocket(fd_);
        fd_ = INVALID_SOCK;
    }
}

void Connection::onReadReady() {
    updateLastActiveTime();
    char buffer[4096];
    int bytesRead = recv(fd_, buffer, sizeof(buffer), 0);

    if (bytesRead > 0) {
        readBuffer_.append(buffer, bytesRead);
        
        // check if headers are fully received
        if (readBuffer_.find("\r\n\r\n") != std::string::npos) {
            processRequest();
        }
    } else if (bytesRead == 0) {
        // client dropped
        isClosing_ = true;
        loop_->removeConnection(fd_);
    } else {
        // hit eagain or real error
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            isClosing_ = true;
            loop_->removeConnection(fd_);
        }
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            isClosing_ = true;
            loop_->removeConnection(fd_);
        }
#endif
    }
}

void Connection::onWriteReady() {
    updateLastActiveTime();
    if (writeBuffer_.empty()) {
        if (isClosing_) {
            loop_->removeConnection(fd_);
        }
        return;
    }

    int bytesSent = send(fd_, writeBuffer_.c_str(), writeBuffer_.size(), 0);
    if (bytesSent > 0) {
        writeBuffer_.erase(0, bytesSent);
        
        if (writeBuffer_.empty()) {
            // done writing, switch back to read
            loop_->modifyConnection(fd_, 1); // 1 = READ
            readBuffer_.clear(); // reset state for next req
        }
    } else {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            isClosing_ = true;
            loop_->removeConnection(fd_);
        }
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            isClosing_ = true;
            loop_->removeConnection(fd_);
        }
#endif
    }
}

void Connection::enqueueResponse(const std::string& data) {
    writeBuffer_.append(data);
    loop_->modifyConnection(fd_, 3); // 3 = IN | OUT
}

void Connection::processRequest() {
    // dispatch to handler
    handleClientRequest(shared_from_this(), readBuffer_);
}
