#include "connection.h"
#include "event_loop.h"
#include "request_handler.h"
#include "thread_pool.h"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <sstream>
#include <string_view>

namespace {
constexpr size_t streamReadSize = 64 * 1024;
constexpr size_t streamBufferLimit = 128 * 1024;
// Reserve space for the chunk framing and possible end marker.
constexpr size_t streamRefillThreshold = streamBufferLimit - streamReadSize - 32;

bool shouldRetrySocket() {
#ifdef _WIN32
    const auto error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}
}

int Connection::server_argc = 0;
char** Connection::server_argv = nullptr;

Connection::Connection(SOCKET_TYPE fd, EventLoop* loop)
    : fd_(fd), loop_(loop), lastActive_(std::chrono::steady_clock::now()) {}

Connection::~Connection() {
    closeConnection();
}

void Connection::runInLoop(std::function<void()> task) {
    loop_->runInLoop(std::move(task));
}

bool Connection::isIdle(int timeoutSeconds) const {
    return std::chrono::steady_clock::now() - lastActive_ > std::chrono::seconds(timeoutSeconds);
}

void Connection::closeConnection() {
    if (fd_ != INVALID_SOCK) {
        if (reportStreamPeak_ && streamPeakBufferedBytes_ > 0) {
            std::clog << "stream_peak_buffer_bytes=" << streamPeakBufferedBytes_ << std::endl;
        }
        closeSocket(fd_);
        fd_ = INVALID_SOCK;
    }
}

void Connection::disconnect() {
    loop_->removeConnection(fd_);
}

void Connection::updateInterest() {
    const int read = responsePending_ ? 0 : 1;
    const int write = writeBuffer_.empty() ? 0 : 2;
    loop_->modifyConnection(fd_, read | write);
}

void Connection::enqueueResponse(const std::string& data) {
    if (fd_ == INVALID_SOCK) return;
    writeBuffer_.append(data);
    updateInterest();
}

void Connection::streamFile(std::string path, bool allowDefaultFallback) {
    closeAfterResponse_ = true;
#ifdef _WIN32
    char reportPeak[2];
    reportStreamPeak_ = GetEnvironmentVariableA("HTTP_STREAM_REPORT_PEAK", reportPeak,
                                                sizeof(reportPeak)) == 1 && reportPeak[0] == '1';
#else
    const auto* reportPeak = std::getenv("HTTP_STREAM_REPORT_PEAK");
    reportStreamPeak_ = reportPeak && std::string_view(reportPeak) == "1";
#endif
    const auto weak = weak_from_this();
    g_threadPool->enqueue([weak, path = std::move(path), allowDefaultFallback] {
        if (weak.expired()) return;
        auto file = std::make_shared<std::ifstream>(path, std::ios::binary);
        if (!*file && allowDefaultFallback) {
            file->open("../files/BAIRI_PIYA.avi", std::ios::binary);
        }
        if (auto conn = weak.lock()) {
            conn->runInLoop([weak, file] {
                auto conn = weak.lock();
                if (!conn || conn->fd_ == INVALID_SOCK) return;
                if (!*file) {
                    conn->enqueueResponse("HTTP/1.1 404 Not Found\r\n"
                                          "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    return;
                }
                conn->stream_ = file;
                conn->enqueueResponse("HTTP/1.1 200 OK\r\n"
                                      "Content-Type: video/x-msvideo\r\n"
                                      "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
                conn->requestStreamRead();
            });
        }
    });
}

void Connection::requestStreamRead() {
    if (!stream_ || streamReadPending_ || writeBuffer_.size() > streamRefillThreshold) return;

    // Reserve one read's worth of capacity before submitting it. Only this job
    // touches the file until its completion runs on the event loop.
    streamReadPending_ = true;
    const auto weak = weak_from_this();
    g_threadPool->enqueue([weak, file = stream_] {
        if (weak.expired()) return;
        std::string data(streamReadSize, '\0');
        file->read(data.data(), data.size());
        data.resize(static_cast<size_t>(file->gcount()));
        const bool failed = file->bad() || (file->fail() && !file->eof());
        const bool eof = file->eof();

        if (auto conn = weak.lock()) {
            conn->runInLoop([weak, data = std::move(data), failed, eof] {
                auto conn = weak.lock();
                if (!conn || conn->fd_ == INVALID_SOCK) return;
                conn->streamReadPending_ = false;
                if (failed) {
                    conn->disconnect();
                    return;
                }
                if (!data.empty()) {
                    std::ostringstream size;
                    size << std::hex << data.size();
                    conn->writeBuffer_.append(size.str()).append("\r\n").append(data).append("\r\n");
                }
                if (eof) {
                    conn->stream_.reset();
                    conn->writeBuffer_.append("0\r\n\r\n");
                }
                assert(conn->writeBuffer_.size() <= streamBufferLimit);
                if (conn->reportStreamPeak_) {
                    conn->streamPeakBufferedBytes_ =
                        (std::max)(conn->streamPeakBufferedBytes_, conn->writeBuffer_.size());
                }
                conn->updateInterest();
                conn->requestStreamRead();
            });
        }
    });
}

void Connection::onReadReady() {
    if (responsePending_) return;
    char buffer[4096];
    const auto bytesRead = recv(fd_, buffer, sizeof(buffer), 0);
    if (bytesRead > 0) {
        lastActive_ = std::chrono::steady_clock::now();
        readBuffer_.append(buffer, bytesRead);
        processRequest();
    } else if (bytesRead == 0 || !shouldRetrySocket()) {
        disconnect();
    }
}

void Connection::onWriteReady() {
    if (writeBuffer_.empty()) return;
#ifdef MSG_NOSIGNAL
    constexpr int sendFlags = MSG_NOSIGNAL;
#else
    constexpr int sendFlags = 0;
#endif
    const auto length = static_cast<int>(std::min<size_t>(writeBuffer_.size(), streamReadSize));
    const auto bytesSent = send(fd_, writeBuffer_.data(), length, sendFlags);
    if (bytesSent > 0) {
        lastActive_ = std::chrono::steady_clock::now();
        writeBuffer_.erase(0, bytesSent);
        requestStreamRead();
        if (writeBuffer_.empty()) finishWrite();
    } else if (bytesSent == 0 || !shouldRetrySocket()) {
        disconnect();
    }
}

void Connection::finishWrite() {
    if (stream_) {
        updateInterest();
        return;
    }
    if (closeAfterResponse_) {
        disconnect();
        return;
    }
    responsePending_ = false;
    processRequest();
    updateInterest();
}

void Connection::processRequest() {
    const auto headerEnd = readBuffer_.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return;
    const auto bodyStart = headerEnd + 4;
    size_t bodySize = 0;
    bool hasLength = false;
    bool invalid = false;
    for (auto lineStart = readBuffer_.find("\r\n") + 2; lineStart < headerEnd;) {
        const auto lineEnd = readBuffer_.find("\r\n", lineStart);
        const std::string_view line(readBuffer_.data() + lineStart, lineEnd - lineStart);
        const auto colon = line.find(':');
        auto name = std::string(line.substr(0, colon));
        for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (name == "content-length" && colon != std::string_view::npos) {
            auto value = line.substr(colon + 1);
            const auto first = value.find_first_not_of(" \t");
            if (first != std::string_view::npos) {
                value = value.substr(first, value.find_last_not_of(" \t") - first + 1);
            }
            const auto end = value.data() + value.size();
            const auto parsed = std::from_chars(value.data(), end, bodySize);
            if (hasLength || parsed.ec != std::errc{} || parsed.ptr != end) invalid = true;
            hasLength = true;
        }
        if (name == "transfer-encoding") invalid = true;
        lineStart = lineEnd + 2;
    }

    const auto requestLineEnd = readBuffer_.find("\r\n");
    const auto methodEnd = readBuffer_.find(' ');
    if (invalid || methodEnd >= requestLineEnd || readBuffer_.find(' ', methodEnd + 1) >= requestLineEnd) {
        responsePending_ = true;
        closeAfterResponse_ = true;
        enqueueResponse("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        return;
    }
    if (bodySize > readBuffer_.size() - bodyStart) return;

    const auto requestSize = bodyStart + bodySize;
    auto request = readBuffer_.substr(0, requestSize);
    readBuffer_.erase(0, requestSize);
    responsePending_ = true;
    updateInterest();
    handleClientRequest(shared_from_this(), request);
}
