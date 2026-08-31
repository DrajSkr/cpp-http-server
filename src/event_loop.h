#pragma once

#include "platform.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <queue>
#include <functional>

class Connection;

class EventLoop {
public:
    virtual ~EventLoop() = default;

    // 1=IN, 2=OUT, 3=BOTH
    virtual void addServerSocket(SOCKET_TYPE fd) = 0;
    virtual void addConnection(SOCKET_TYPE fd, std::shared_ptr<Connection> conn, int events) = 0;
    virtual void modifyConnection(SOCKET_TYPE fd, int events) = 0;
    virtual void removeConnection(SOCKET_TYPE fd) = 0;
    
    virtual void run() = 0;

    void runInLoop(std::function<void()> task) {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        pending_tasks_.push(std::move(task));
    }

    static std::unique_ptr<EventLoop> create();

protected:
    std::unordered_map<SOCKET_TYPE, std::shared_ptr<Connection>> connections_;
    void checkTimeouts();
    
    std::queue<std::function<void()>> pending_tasks_;
    std::mutex tasks_mutex_;

    void executeTasks() {
        std::queue<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            std::swap(tasks, pending_tasks_);
        }
        while (!tasks.empty()) {
            tasks.front()();
            tasks.pop();
        }
    }
};
