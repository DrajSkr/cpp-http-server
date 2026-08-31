#include "request_handler.h"
#include "connection.h"
#include <iostream>
#include <cstring>
#include <regex>
#include <cstdlib>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <zlib.h>
#include <string>

void handleRootRoute(std::shared_ptr<Connection> conn)
{
    std::string responseBody = "<html><body><h1>But In The End, Every Death Is Just A New Beginning.</h1></body></html>";
    std::string response = 
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: " + std::to_string(responseBody.size()) + "\r\n"
    "\r\n" +
    responseBody;

    conn->enqueueResponse(response);
}

void handleEchoRoute(std::shared_ptr<Connection> conn, std::string &path, const std::string& requestStr)
{
    size_t encodingHeaderPos = requestStr.find("Accept-Encoding:");
    bool acceptsGzip = false;

    if (encodingHeaderPos != std::string::npos)
    {
        size_t encodingHeaderEndPos = requestStr.find("\r\n", encodingHeaderPos);
        if (encodingHeaderEndPos != std::string::npos)
        {
            std::string encodingHeader = requestStr.substr(encodingHeaderPos, encodingHeaderEndPos - encodingHeaderPos);
            acceptsGzip = encodingHeader.find("gzip") != std::string::npos;
        }
    }

    if (acceptsGzip)
    {
        std::string content = path.substr(6);

        z_stream zs;
        memset(&zs, 0, sizeof(zs));

        if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        {
            throw(std::runtime_error("deflateInit failed while compressing."));
        }
        zs.next_in = (Bytef *)content.data();
        zs.avail_in = content.size();

        int ret;
        char outbuffer[32768];
        std::string compressedContent;

        do
        {
            zs.next_out = reinterpret_cast<Bytef *>(outbuffer);
            zs.avail_out = sizeof(outbuffer);

            ret = deflate(&zs, Z_FINISH);

            if (compressedContent.size() < zs.total_out)
            {
                compressedContent.append(outbuffer, zs.total_out - compressedContent.size());
            }
        } while (ret == Z_OK);

        deflateEnd(&zs);
        if (ret != Z_STREAM_END)
        { 
            std::ostringstream oss;
            oss << "Exception during zlib compression: (" << ret << ") " << zs.msg;
            throw(std::runtime_error(oss.str()));
        }

        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Encoding: gzip\r\nContent-Length: " + std::to_string(compressedContent.size()) + "\r\n\r\n" + compressedContent;
        conn->enqueueResponse(response);
    }
    else
    {
        std::string content = path.substr(6);
        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(content.size()) + "\r\n\r\n" + content;
        conn->enqueueResponse(response);
    }
}

void handleUserAgentRoute(std::shared_ptr<Connection> conn, const std::string& requestStr)
{
    std::string userAgentHeader = "User-Agent: ";
    size_t ua_start = requestStr.find(userAgentHeader);

    if (ua_start != std::string::npos)
    {
        size_t ua_end = requestStr.find("\r\n", ua_start);
        std::string userAgent = requestStr.substr(ua_start + userAgentHeader.size(), ua_end - ua_start - userAgentHeader.size());
        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(userAgent.size()) + "\r\n\r\n" + userAgent;
        conn->enqueueResponse(response);
    }
    else
    {
        handleNotFound(conn);
    }
}

void handleNotFound(std::shared_ptr<Connection> conn)
{
    std::string message = "HTTP/1.1 404 Not Found\r\n\r\n";
    conn->enqueueResponse(message);
}

#include "thread_pool.h"

void handleFilesRoute(std::shared_ptr<Connection> conn, int argc, char **argv, std::string &path, const std::string& method, const std::string& requestStr)
{
    std::string directory = argv[2];
    std::string filename = path.substr(7);
    std::string filePath = directory + filename;

    std::weak_ptr<Connection> weak_conn = conn;

    if (method == "GET")
    {
        g_threadPool->enqueue([weak_conn, filePath]() {
            if (std::filesystem::exists(filePath))
            {
                std::ifstream file(filePath, std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(content.size()) + "\r\n\r\n" + content;
                
                if (auto locked_conn = weak_conn.lock()) {
                    locked_conn->runInLoop([weak_conn, response]() {
                        if (auto c = weak_conn.lock()) c->enqueueResponse(response);
                    });
                }
            }
            else
            {
                if (auto locked_conn = weak_conn.lock()) {
                    locked_conn->runInLoop([weak_conn]() {
                        if (auto c = weak_conn.lock()) handleNotFound(c);
                    });
                }
            }
        });
    }
    else if (method == "POST")
    {
        size_t body_pos = requestStr.find("\r\n\r\n");
        if (body_pos != std::string::npos)
        {
            std::string body = requestStr.substr(body_pos + 4);
            g_threadPool->enqueue([weak_conn, filePath, body]() {
                std::ofstream file(filePath, std::ios::binary);
                file << body;
                file.close();

                std::string response = "HTTP/1.1 201 Created\r\n\r\n";
                if (auto locked_conn = weak_conn.lock()) {
                    locked_conn->runInLoop([weak_conn, response]() {
                        if (auto c = weak_conn.lock()) c->enqueueResponse(response);
                    });
                }
            });
        }
        else
        {
            std::string response = "HTTP/1.1 400 Bad Request\r\n\r\n";
            conn->enqueueResponse(response);
        }
    }
    else
    {
        handleNotFound(conn);
    }
}

void handleStreamRoute(std::shared_ptr<Connection> conn) {
    std::weak_ptr<Connection> weak_conn = conn;
    g_threadPool->enqueue([weak_conn]() {
        std::ifstream file("../files/BAIRI_PIYA.avi", std::ios::binary);
        if (!file.is_open()) {
            std::string error = "HTTP/1.1 404 Not Found\r\n\r\nVideo file not found";
            if (auto locked_conn = weak_conn.lock()) {
                locked_conn->runInLoop([weak_conn, error]() {
                    if (auto c = weak_conn.lock()) c->enqueueResponse(error);
                });
            }
            return;
        }

        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::string header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: video/x-msvideo\r\n"
            "Content-Length: " + std::to_string(size) + "\r\n"
            "Connection: close\r\n\r\n";

        if (auto locked_conn = weak_conn.lock()) {
            locked_conn->runInLoop([weak_conn, header]() {
                if (auto c = weak_conn.lock()) c->enqueueResponse(header);
            });
        }

        const size_t bufferSize = 8192;
        char buffer[bufferSize];
        while (file.read(buffer, bufferSize) || file.gcount() > 0) {
            std::string chunk(buffer, file.gcount());
            if (auto locked_conn = weak_conn.lock()) {
                locked_conn->runInLoop([weak_conn, chunk]() {
                    if (auto c = weak_conn.lock()) c->enqueueResponse(chunk);
                });
            } else {
                // Client disconnected! Abort the file read loop.
                break;
            }
        }
    });
}

void handleClientRequest(std::shared_ptr<Connection> conn, const std::string& requestStr)
{
    if (requestStr.empty()) return;

    size_t first_space = requestStr.find(' ');
    if (first_space == std::string::npos) return;
    std::string method = requestStr.substr(0, first_space);

    size_t second_space = requestStr.find(' ', first_space + 1);
    if (second_space == std::string::npos) return;
    std::string path = requestStr.substr(first_space + 1, second_space - first_space - 1);

    std::regex pattern("^/echo/.+$");
    std::regex filesPattern("^/files/[^/]+$");

    if (path == "/")
    {
        handleRootRoute(conn);
    }
    else if (std::regex_match(path, pattern))
    {
        handleEchoRoute(conn, path, requestStr);
    }
    else if (path == "/user-agent")
    {
        handleUserAgentRoute(conn, requestStr);
    }
    else if(path == "/stream"){    
        handleStreamRoute(conn);
    }
    else if (std::regex_match(path, filesPattern) && Connection::server_argc == 3 && strcmp(Connection::server_argv[1], "--directory") == 0)
    {
        handleFilesRoute(conn, Connection::server_argc, Connection::server_argv, path, method, requestStr);
    }
    else
    {
        handleNotFound(conn);
    }
}