#include "event_loop.h"
#include "connection.h"
#include <iostream>
#include <algorithm>
#include <chrono>

#ifdef _WIN32
// win32 uses WSAPoll
#else
#include <poll.h>
#endif

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
        connections_.erase(fd);
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
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
                            conn->closeConnection();
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
                            conn->closeConnection();
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
        auto conn = connections_[fd];
        conn->closeConnection();
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
