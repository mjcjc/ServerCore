#pragma once

#include "MasterProtocol.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct GameServerInfo
{
    std::uint32_t serverId = 0;
    std::string ip;
    std::uint16_t port = 0;
    std::uint32_t currentSessions = 0;
    std::uint32_t maxSessions = 0;
    std::chrono::steady_clock::time_point lastHeartbeat;
    bool active = false;
};

class GameServerRegistry
{
  public:
    void Register(const MasterMessage& message);
    void UpdateHeartbeat(const MasterMessage& message);
    void MarkTimedOut(std::chrono::seconds timeout);

    std::vector<GameServerInfo> GetAll() const;
    std::optional<GameServerInfo> SelectLeastLoaded() const;

  private:
    mutable std::mutex mutex;
    std::unordered_map<std::uint32_t, GameServerInfo> servers;
};
