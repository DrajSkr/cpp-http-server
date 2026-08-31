#include "connection.h"
#include "event_loop.h"
#include "request_handler.h"
#include <iostream>

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
        
        // Simple state machine: wait for complete HTTP headers
        if (readBuffer_.find("\r\n\r\n") != std::string::npos) {
            processRequest();
        }
    } else if (bytesRead == 0) {
        // Client disconnected gracefully
        isClosing_ = true;
        loop_->removeConnection(fd_);
    } else {
        // Error or EAGAIN/WSAEWOULDBLOCK
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
            // Done writing. We can either keep-alive or close. 
            // For this project, HTTP/1.1 usually keep-alive, but let's assume we close after response for simplicity,
            // or wait for the next request. Let's just wait for next request (keep-alive).
            // We need to switch monitoring back to READ only.
            loop_->modifyConnection(fd_, 1); // 1 = READ
            readBuffer_.clear(); // Very rudimentary reset, assume full request was parsed
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
    loop_->modifyConnection(fd_, 3); // 3 = READ | WRITE
}

void Connection::processRequest() {
    // Pass the full request to the handler
    handleClientRequest(shared_from_this(), readBuffer_);
}
