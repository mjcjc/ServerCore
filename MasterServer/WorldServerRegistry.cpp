#include "WorldServerRegistry.h"

#include <algorithm>
#include <cstdio>

namespace
{
    WorldServerInfo CreateWorldServerInfo(const MasterMessage& message)
    {
        WorldServerInfo info;
        info.serverId = message.serverId;
        info.worldId = message.worldId;
        info.ip = message.ip;
        info.port = message.port;
        info.lastHeartbeat = std::chrono::steady_clock::now();
        info.active = true;
        return info;
    }
}

void WorldServerRegistry::Register(const MasterMessage& message)
{
    std::lock_guard<std::mutex> lock(mutex);
    const bool isNewServer = servers.find(message.serverId) == servers.end();
    servers[message.serverId] = CreateWorldServerInfo(message);

    printf("[Registry] WorldServer %u %s. World=%u, Endpoint=%s:%u\n",
           message.serverId,
           isNewServer ? "registered" : "re-registered",
           message.worldId,
           message.ip,
           message.port);
}

void WorldServerRegistry::UpdateHeartbeat(const MasterMessage& message)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto iterator = servers.find(message.serverId);

    if (iterator == servers.end())
    {
        servers[message.serverId] = CreateWorldServerInfo(message);
        printf("[Registry] WorldServer %u recovered by heartbeat.\n", message.serverId);
        return;
    }

    WorldServerInfo& info = iterator->second;
    const bool wasInactive = !info.active;
    info.worldId = message.worldId;
    info.ip = message.ip;
    info.port = message.port;
    info.lastHeartbeat = std::chrono::steady_clock::now();
    info.active = true;

    if (wasInactive)
    {
        printf("[Registry] WorldServer %u is active again.\n", message.serverId);
    }
}

void WorldServerRegistry::MarkTimedOut(std::chrono::seconds timeout)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto now = std::chrono::steady_clock::now();

    for (auto& [serverId, info] : servers)
    {
        if (info.active && now - info.lastHeartbeat > timeout)
        {
            info.active = false;
            printf("[Registry] WorldServer %u timed out and is now inactive.\n", serverId);
        }
    }
}

std::vector<WorldServerInfo> WorldServerRegistry::GetAll() const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<WorldServerInfo> result;
    result.reserve(servers.size());

    for (const auto& [serverId, info] : servers)
    {
        result.push_back(info);
    }

    std::sort(result.begin(), result.end(), [](const WorldServerInfo& left, const WorldServerInfo& right)
    {
        return left.serverId < right.serverId;
    });
    return result;
}
