#include <iostream>
#include <cstdlib>
#include <string>
#include "platform.h"
#include "event_loop.h"
#include "connection.h"
#include "thread_pool.h"

ThreadPool* g_threadPool = nullptr;

int main(int argc, char **argv)
{
    g_threadPool = new ThreadPool(8);

    if (!initPlatform()) {
        std::cerr << "Failed to initialize platform socket library\n";
        return 1;
    }

    // setup args for routes
    Connection::server_argc = argc;
    Connection::server_argv = argv;

    SOCKET_TYPE server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCK) {
        std::cerr << "Failed to create server socket\n";
        cleanupPlatform();
        return 1;
    }

    int reuse = 1;
#ifdef _WIN32
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)) < 0)
#else
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0)
#endif
    {
        std::cerr << "setsockopt failed\n";
        closeSocket(server_fd);
        cleanupPlatform();
        return 1;
    }

    makeSocketNonBlocking(server_fd);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(4221);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 4221\n";
        closeSocket(server_fd);
        cleanupPlatform();
        return 1;
    }

    // backlog
    int connection_backlog = 10000;
    if (listen(server_fd, connection_backlog) != 0) {
        std::cerr << "listen failed\n";
        closeSocket(server_fd);
        cleanupPlatform();
        return 1;
    }

    std::cout << "Event Loop Server started on port 4221...\n";

    auto loop = EventLoop::create();
    loop->addServerSocket(server_fd);
    
    // spin forever
    loop->run();

    closeSocket(server_fd);
    cleanupPlatform();
    return 0;
}
