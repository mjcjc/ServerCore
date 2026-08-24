#pragma once

#include <WinSock2.h>

#include "ServerConfig.h"
#include "../WorldServer/WorldProtocol.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class NetworkCore;

class WorldClient
{
  public:
    WorldClient(const ServerConfig& config, NetworkCore& server);
    ~WorldClient();

    void Start();
    void Stop();
    bool SendWorldChat(std::int32_t userId, const std::string& nickname, const char* message);

  private:
    void Run();
    bool SendWorldMessage(const WorldMessage& message);

    const ServerConfig& config;
    NetworkCore& server;
    std::atomic<bool> running{false};
    std::atomic<SOCKET> currentSocket{INVALID_SOCKET};
    std::mutex sendMutex;
    std::thread worker;
};
