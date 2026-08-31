#pragma once

// socket helpers to hide win32/posix differences
#ifdef _WIN32
    // --- Windows (Winsock2) ---
    // MUST precede windows.h
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>

    // link ws2_32
    #pragma comment(lib, "ws2_32.lib")

    using SOCKET_TYPE = SOCKET;
    constexpr SOCKET_TYPE INVALID_SOCK = INVALID_SOCKET;

    inline bool initPlatform() {
        WSADATA wsaData;
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }

    inline void cleanupPlatform() {
        WSACleanup();
    }

    inline int closeSocket(SOCKET_TYPE sock) {
        return closesocket(sock);
    }

    inline bool makeSocketNonBlocking(SOCKET_TYPE sock) {
        u_long mode = 1;
        return ioctlsocket(sock, FIONBIO, &mode) == 0;
    }

#else
    // --- Linux / macOS (POSIX) ---
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>

    using SOCKET_TYPE = int;
    constexpr SOCKET_TYPE INVALID_SOCK = -1;

    inline bool initPlatform() {
        return true; // no-op on posix
    }

    inline void cleanupPlatform() {
        // no-op
    }

    inline int closeSocket(SOCKET_TYPE sock) {
        return close(sock);
    }

    inline bool makeSocketNonBlocking(SOCKET_TYPE sock) {
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags == -1) return false;
        return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
    }

#endif
