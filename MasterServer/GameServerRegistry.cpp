#include "GameServerRegistry.h"

#include <algorithm>
#include <cstdio>

namespace
{
    GameServerInfo CreateServerInfo(const MasterMessage& message)
    {
        GameServerInfo info;
        info.serverId = message.serverId;
        info.worldId = message.worldId;
        info.channelId = message.channelId;
        info.ip = message.ip;
        info.port = message.port;
        info.currentSessions = message.currentSessions;
        info.maxSessions = message.maxSessions;
        info.lastHeartbeat = std::chrono::steady_clock::now();
        info.active = true;
        return info;
    }
}

void GameServerRegistry::Register(const MasterMessage& message)
{
    std::lock_guard<std::mutex> lock(mutex);
    const bool isNewServer = servers.find(message.serverId) == servers.end();
    servers[message.serverId] = CreateServerInfo(message);

    printf("[Registry] GameServer %u %s. World=%u, Channel=%u, Endpoint=%s:%u, Sessions=%u/%u\n",
           message.serverId,
           isNewServer ? "registered" : "re-registered",
           message.worldId,
           message.channelId,
           message.ip,
           message.port,
           message.currentSessions,
           message.maxSessions);
}

void GameServerRegistry::UpdateHeartbeat(const MasterMessage& message)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto iterator = servers.find(message.serverId);

    if (iterator == servers.end())
    {
        servers[message.serverId] = CreateServerInfo(message);
        printf("[Registry] GameServer %u recovered by heartbeat.\n", message.serverId);
        return;
    }

    GameServerInfo& info = iterator->second;
    const bool wasInactive = !info.active;
    info.ip = message.ip;
    info.port = message.port;
    info.worldId = message.worldId;
    info.channelId = message.channelId;
    info.currentSessions = message.currentSessions;
    info.maxSessions = message.maxSessions;
    info.lastHeartbeat = std::chrono::steady_clock::now();
    info.active = true;

    if (wasInactive)
    {
        printf("[Registry] GameServer %u is active again.\n", message.serverId);
    }
}

void GameServerRegistry::MarkTimedOut(std::chrono::seconds timeout)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto now = std::chrono::steady_clock::now();

    for (auto& [serverId, info] : servers)
    {
        if (info.active && now - info.lastHeartbeat > timeout)
        {
            info.active = false;
            printf("[Registry] GameServer %u timed out and is now inactive.\n", serverId);
        }
    }
}

std::vector<GameServerInfo> GameServerRegistry::GetAll() const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<GameServerInfo> result;
    result.reserve(servers.size());

    for (const auto& [serverId, info] : servers)
    {
        result.push_back(info);
    }

    std::sort(result.begin(), result.end(), [](const GameServerInfo& left, const GameServerInfo& right)
    {
        return left.serverId < right.serverId;
    });
    return result;
}

std::optional<GameServerInfo> GameServerRegistry::SelectLeastLoaded(std::uint32_t worldId,
                                                                    std::uint32_t channelId) const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::optional<GameServerInfo> selected;

    for (const auto& [serverId, info] : servers)
    {
        if (!info.active || info.currentSessions >= info.maxSessions)
        {
            continue;
        }

        if ((worldId != 0 && info.worldId != worldId) ||
            (channelId != 0 && info.channelId != channelId))
        {
            continue;
        }

        if (!selected || info.currentSessions < selected->currentSessions ||
            (info.currentSessions == selected->currentSessions &&
             info.serverId < selected->serverId))
        {
            selected = info;
        }
    }

    return selected;
}
