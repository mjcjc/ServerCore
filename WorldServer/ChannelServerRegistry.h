#pragma once

#include "WorldProtocol.h"

#include <WinSock2.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

struct ChannelServerInfo
{
    std::uint32_t serverId = 0;
    std::uint32_t worldId = 0;
    std::uint32_t channelId = 0;
    std::string ip;
    std::uint16_t port = 0;
    std::uint32_t currentSessions = 0;
    std::chrono::steady_clock::time_point lastHeartbeat;
    bool active = false;
    SOCKET socket = INVALID_SOCKET;
};

class ChannelServerRegistry
{
  public:
    void Register(const WorldMessage& message, SOCKET socket);
    void UpdateHeartbeat(const WorldMessage& message);
    void MarkTimedOut(std::chrono::seconds timeout);
    void Remove(SOCKET socket);
    void BroadcastWorldChat(const WorldMessage& message);

  private:
    mutable std::mutex mutex;
    std::unordered_map<std::uint32_t, ChannelServerInfo> servers;
};
