# HTTP Server in C++

This project is a custom HTTP/1.1 server implemented in C++ (using C++20). It was originally built as a solution to the ["Build Your Own HTTP server" Challenge](https://app.codecrafters.io/courses/http-server/overview) and has been extended with an event-driven architecture, a custom thread pool, and additional routing capabilities.

## Overview

The server is capable of handling multiple client connections concurrently and efficiently. It uses an **event loop** for non-blocking I/O and a **thread pool** to offload file operations and streaming, preventing the main event loop from blocking.

### Features
- **Concurrent Connections**: Handles multiple connections concurrently using a non-blocking event-driven architecture.
- **Thread Pool**: Uses a custom thread pool for background tasks like file reading/writing and streaming large files.
- **Gzip Compression**: Supports gzip compression for responses using `zlib` (e.g., in the `/echo/` route).
- **Video Streaming**: Asynchronously streams large video files (e.g., `.avi`) in chunks.
- **Cross-Platform Support**: Includes a platform abstraction layer (`platform.h`), making the socket implementation compatible with both Windows (Winsock2) and POSIX-based systems (Linux/macOS).

## Endpoints

The server listens on port `4221` and supports the following routes:

- `GET /`
  Returns a simple HTML greeting.
- `GET /echo/{string}`
  Echoes back the `{string}` in the response. If the client includes `Accept-Encoding: gzip` in the request, the response is compressed using zlib.
- `GET /user-agent`
  Reads the `User-Agent` header from the request and returns it as plain text.
- `GET /files/{filename}`
  Reads the specified file from the directory provided via command-line arguments and returns its contents.
- `POST /files/{filename}`
  Creates or overwrites the specified file in the given directory with the request body.
- `GET /stream`
  Streams a video file (`BAIRI_PIYA.avi`) back to the client in asynchronous chunks.

## Build Instructions

### Prerequisites
- CMake (3.13 or newer)
- A C++20 compatible compiler (e.g., GCC, Clang, MSVC)
- [zlib](https://zlib.net/) (can be installed via vcpkg or system package manager)

### Building the Project

1. Ensure you have `cmake` installed locally.
2. Create a build directory and configure the project:
   ```sh
   mkdir build
   cd build
   cmake ..
   ```
   *(Note: If using vcpkg, the `CMakeLists.txt` automatically detects the vcpkg toolchain if placed in the `vcpkg/` directory).*
3. Build the server:
   ```sh
   cmake --build .
   ```

## Running the Server

Run the compiled executable. You can optionally specify a directory for the `/files/` route:

```sh
# Basic run
./server

# Run with a directory specified for file endpoints
./server --directory /path/to/serve/files/
```

### Running on WSL (Windows Subsystem for Linux)
If you are running the server inside WSL and want to access it from Windows, you may need to forward the port:
1. Find your WSL IP using `ip addr show eth0`.
2. Open Windows PowerShell as Administrator and run:
   ```ps1
   netsh interface portproxy add v4tov4 listenport=4221 listenaddress=0.0.0.0 connectport=4221 connectaddress=<WSL_IP>
   ```
3. Allow the port in the Windows Firewall:
   ```ps1
   netsh advfirewall firewall add rule name="WSL Port 4221" dir=in action=allow protocol=TCP localport=4221
   ```
