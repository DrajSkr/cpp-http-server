#include "event_loop.h"
#include "connection.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <stdexcept>

#ifdef _WIN32
// win32 uses WSAPoll
#else
#include <poll.h>
#endif

EventLoop::EventLoop() {
    // A loopback datagram wakes both epoll and WSAPoll for worker completions.
    wakeupReceiver_ = socket(AF_INET, SOCK_DGRAM, 0);
    wakeupSender_ = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof(address);
    if (wakeupReceiver_ == INVALID_SOCK || wakeupSender_ == INVALID_SOCK ||
        !makeSocketNonBlocking(wakeupReceiver_) || !makeSocketNonBlocking(wakeupSender_) ||
        bind(wakeupReceiver_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        getsockname(wakeupReceiver_, reinterpret_cast<sockaddr*>(&address), &length) != 0 ||
        connect(wakeupSender_, reinterpret_cast<sockaddr*>(&address), length) != 0 ||
        getsockname(wakeupSender_, reinterpret_cast<sockaddr*>(&address), &length) != 0 ||
        connect(wakeupReceiver_, reinterpret_cast<sockaddr*>(&address), length) != 0) {
        if (wakeupReceiver_ != INVALID_SOCK) closeSocket(wakeupReceiver_);
        if (wakeupSender_ != INVALID_SOCK) closeSocket(wakeupSender_);
        throw std::runtime_error("Failed to initialize event loop wakeup");
    }
}

EventLoop::~EventLoop() {
    closeSocket(wakeupReceiver_);
    closeSocket(wakeupSender_);
}

void EventLoop::runInLoop(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        pending_tasks_.push(std::move(task));
        if (pending_tasks_.size() > 1) return;
    }
    // A full receive queue already has a wakeup pending. The poll timeout
    // also gives the event loop a chance to collect work if a send fails.
    const char signal = 0;
    send(wakeupSender_, &signal, 1, 0);
}

void EventLoop::drainWakeup() {
    char signal;
    while (recv(wakeupReceiver_, &signal, 1, 0) > 0) {}
}

#ifdef __linux__
#include <sys/epoll.h>

class EpollBackend : public EventLoop {
public:
    EpollBackend() {
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ == -1) {
            std::cerr << "Failed to create epoll file descriptor\n";
            exit(EXIT_FAILURE);
        }
        epoll_event wakeup{};
        wakeup.events = EPOLLIN;
        wakeup.data.fd = wakeupReceiver_;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeupReceiver_, &wakeup) == -1) {
            closeSocket(epoll_fd_);
            throw std::runtime_error("Failed to register event loop wakeup");
        }
    }

    ~EpollBackend() override {
        if (epoll_fd_ != -1) {
            closeSocket(epoll_fd_);
        }
    }

    void addServerSocket(SOCKET_TYPE fd) override {
        server_fd_ = fd;
        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
            std::cerr << "Failed to add server socket to epoll\n";
        }
    }

    void addConnection(SOCKET_TYPE fd, std::shared_ptr<Connection> conn, int events) override {
        connections_[fd] = conn;
        struct epoll_event event;
        event.events = 0;
        if (events & 1) event.events |= EPOLLIN;
        if (events & 2) event.events |= EPOLLOUT;
        event.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
            std::cerr << "Failed to add client socket to epoll\n";
        }
    }

    void modifyConnection(SOCKET_TYPE fd, int events) override {
        struct epoll_event event;
        event.events = 0;
        if (events & 1) event.events |= EPOLLIN;
        if (events & 2) event.events |= EPOLLOUT;
        event.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event);
    }

    void removeConnection(SOCKET_TYPE fd) override {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        connections_.at(fd)->closeConnection();
        connections_.erase(fd);
    }

    void run() override {
        const int MAX_EVENTS = 10000;
        struct epoll_event events[MAX_EVENTS];

        while (true) {
            checkTimeouts();
            executeTasks();

            int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, 50);
            if (num_events == -1) {
                std::cerr << "epoll_wait error\n";
                break;
            }

            for (int i = 0; i < num_events; i++) {
                int fd = events[i].data.fd;

                if (fd == wakeupReceiver_) {
                    drainWakeup();
                    continue;
                }
                if (fd == server_fd_) {
                    if (events[i].events & EPOLLIN) {
                        struct sockaddr_storage client_addr;
                        socklen_t client_addr_len = sizeof(client_addr);
                        SOCKET_TYPE client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_addr_len);
                        
                        if (client_fd != INVALID_SOCK) {
                            makeSocketNonBlocking(client_fd);
                            auto conn = std::make_shared<Connection>(client_fd, this);
                            addConnection(client_fd, conn, 1);
                        }
                    }
                } else {
                    auto it = connections_.find(fd);
                    if (it != connections_.end()) {
                        auto conn = it->second;
                        if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                            removeConnection(fd);
                        } else {
                            if (events[i].events & EPOLLIN) {
                                conn->onReadReady();
                            }
                            if (connections_.find(fd) != connections_.end() && (events[i].events & EPOLLOUT)) {
                                conn->onWriteReady();
                            }
                        }
                    }
                }
            }
        }
    }

private:
    int epoll_fd_ = -1;
    SOCKET_TYPE server_fd_ = INVALID_SOCK;
};

#endif

class PollBackend : public EventLoop {
public:
    PollBackend() {
        poll_fds_.push_back({wakeupReceiver_, POLLIN, 0});
    }

    void addServerSocket(SOCKET_TYPE fd) override {
        server_fd_ = fd;
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        poll_fds_.push_back(pfd);
    }

    void addConnection(SOCKET_TYPE fd, std::shared_ptr<Connection> conn, int events) override {
        connections_[fd] = conn;
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = 0;
        if (events & 1) pfd.events |= POLLIN;
        if (events & 2) pfd.events |= POLLOUT;
        pfd.revents = 0;
        poll_fds_.push_back(pfd);
    }

    void modifyConnection(SOCKET_TYPE fd, int events) override {
        for (auto& pfd : poll_fds_) {
            if (pfd.fd == fd) {
                pfd.events = 0;
                if (events & 1) pfd.events |= POLLIN;
                if (events & 2) pfd.events |= POLLOUT;
                break;
            }
        }
    }

    void removeConnection(SOCKET_TYPE fd) override {
        connections_.at(fd)->closeConnection();
        connections_.erase(fd);
        poll_fds_.erase(
            std::remove_if(poll_fds_.begin(), poll_fds_.end(),
                [fd](const pollfd& pfd) { return pfd.fd == fd; }),
            poll_fds_.end()
        );
    }

    void run() override {
        while (true) {
            checkTimeouts();
            executeTasks();
            
#ifdef _WIN32
            int ret = WSAPoll(poll_fds_.data(), poll_fds_.size(), 50);
#else
            int ret = poll(poll_fds_.data(), poll_fds_.size(), 50);
#endif
            if (ret < 0) {
                std::cerr << "poll error\n";
                break;
            }

            if (ret == 0) continue; 

            std::vector<pollfd> current_fds = poll_fds_;

            for (const auto& pfd : current_fds) {
                if (pfd.revents == 0) continue;

                if (pfd.fd == wakeupReceiver_) {
                    drainWakeup();
                    continue;
                }
                if (pfd.fd == server_fd_) {
                    if (pfd.revents & POLLIN) {
                        struct sockaddr_storage client_addr;
                        socklen_t client_addr_len = sizeof(client_addr);
                        SOCKET_TYPE client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_addr_len);
                        
                        if (client_fd != INVALID_SOCK) {
                            makeSocketNonBlocking(client_fd);
                            auto conn = std::make_shared<Connection>(client_fd, this);
                            addConnection(client_fd, conn, 1); 
                        }
                    }
                } else {
                    auto it = connections_.find(pfd.fd);
                    if (it != connections_.end()) {
                        auto conn = it->second;
                        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                            removeConnection(pfd.fd);
                        } else {
                            if (pfd.revents & POLLIN) {
                                conn->onReadReady();
                            }
                            if (connections_.find(pfd.fd) != connections_.end() && (pfd.revents & POLLOUT)) {
                                conn->onWriteReady();
                            }
                        }
                    }
                }
            }
        }
    }

private:
    SOCKET_TYPE server_fd_ = INVALID_SOCK;
    std::vector<pollfd> poll_fds_;
};

void EventLoop::checkTimeouts() {
    std::vector<SOCKET_TYPE> to_remove;
    for (const auto& pair : connections_) {
        // nuke idle connections (slowloris)
        if (pair.second->isIdle(5)) {
            to_remove.push_back(pair.first);
        }
    }

    for (SOCKET_TYPE fd : to_remove) {
        std::cout << "[Timeout] Reaping idle connection fd=" << fd << "\n";
        removeConnection(fd);
    }
}

std::unique_ptr<EventLoop> EventLoop::create() {
#ifdef __linux__
    std::cout << "EventLoop: Using EpollBackend\n";
    return std::make_unique<EpollBackend>();
#else
    std::cout << "EventLoop: Using PollBackend\n";
    return std::make_unique<PollBackend>();
#endif
}
