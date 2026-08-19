#pragma once

#include <cstdint>
#include <string>

struct ServerConfig
{
    std::uint32_t serverId = 1;
    std::string bindIp = "0.0.0.0";
    std::uint16_t port = 9000;
    std::uint32_t workerCount = 4;
    bool skipDatabase = false;

    static bool Parse(int argc, char* argv[], ServerConfig& config, std::string& error);

    static const char* Usage();
};
