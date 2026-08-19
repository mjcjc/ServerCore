#pragma once

#include "ServerConfig.h"

#include <atomic>
#include <cstdint>
#include <thread>

class NetworkCore;

class MasterClient
{
  public:
    MasterClient(const ServerConfig& config, const NetworkCore& server);
    ~MasterClient();

    void Start();
    void Stop();

  private:
    void Run();

    const ServerConfig& config;
    const NetworkCore& server;
    std::atomic<bool> running{false};
    std::thread worker;
};
