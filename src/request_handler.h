#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include <string>
#include "platform.h"

#include <memory>
class Connection;

void handleStreamRoute(std::shared_ptr<Connection> conn);
void handleRootRoute(std::shared_ptr<Connection> conn);
void handleEchoRoute(std::shared_ptr<Connection> conn, std::string &path, const std::string& requestStr);
void handleUserAgentRoute(std::shared_ptr<Connection> conn, const std::string& requestStr);
void handleNotFound(std::shared_ptr<Connection> conn);
void handleFilesRoute(std::shared_ptr<Connection> conn, int argc, char **argv, std::string &path, const std::string& method, const std::string& requestStr);

void handleClientRequest(std::shared_ptr<Connection> conn, const std::string& requestStr);

#endif