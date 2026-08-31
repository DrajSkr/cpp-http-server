#pragma once

// ============================================================
// platform.h — Cross-platform socket abstraction
//
// Centralizes all #ifdef _WIN32 logic so the rest of the
// codebase can use a single, portable API:
//
//   SOCKET_TYPE   — socket descriptor type
//   INVALID_SOCK  — sentinel value for failed socket calls
//   closeSocket() — close a socket descriptor
//   initPlatform()    — call once at startup (WSAStartup on Win)
//   cleanupPlatform() — call once at shutdown (WSACleanup on Win)
// ============================================================

#ifdef _WIN32
    // --- Windows (Winsock2) ---
    // winsock2.h MUST come before windows.h to avoid conflicts
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>

    // Link against the Winsock2 library automatically (MSVC)
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
        return true; // No initialization needed on POSIX
    }

    inline void cleanupPlatform() {
        // No cleanup needed on POSIX
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
