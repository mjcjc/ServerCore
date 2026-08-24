#pragma once

#include "MasterProtocol.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct WorldServerInfo
{
    std::uint32_t serverId = 0;
    std::uint32_t worldId = 0;
    std::string ip;
    std::uint16_t port = 0;
    std::chrono::steady_clock::time_point lastHeartbeat;
    bool active = false;
};

class WorldServerRegistry
{
  public:
    void Register(const MasterMessage& message);
    void UpdateHeartbeat(const MasterMessage& message);
    void MarkTimedOut(std::chrono::seconds timeout);

    std::vector<WorldServerInfo> GetAll() const;

  private:
    mutable std::mutex mutex;
    std::unordered_map<std::uint32_t, WorldServerInfo> servers;
};
