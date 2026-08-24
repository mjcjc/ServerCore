#include "ChannelServerRegistry.h"

#include <cstdio>

namespace
{
    ChannelServerInfo CreateChannelServerInfo(const WorldMessage& message, SOCKET socket)
    {
        ChannelServerInfo info;
        info.serverId = message.serverId;
        info.worldId = message.worldId;
        info.channelId = message.channelId;
        info.ip = message.ip;
        info.port = message.port;
        info.currentSessions = message.currentSessions;
        info.lastHeartbeat = std::chrono::steady_clock::now();
        info.active = true;
        info.socket = socket;
        return info;
    }
}

void ChannelServerRegistry::Register(const WorldMessage& message, SOCKET socket)
{
    std::lock_guard<std::mutex> lock(mutex);
    servers[message.serverId] = CreateChannelServerInfo(message, socket);

    printf("[WorldRegistry] GameServer %u registered. World=%u, Channel=%u, Endpoint=%s:%u\n",
           message.serverId,
           message.worldId,
           message.channelId,
           message.ip,
           message.port);
}

void ChannelServerRegistry::UpdateHeartbeat(const WorldMessage& message)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto iterator = servers.find(message.serverId);

    if (iterator == servers.end())
    {
        servers[message.serverId] = CreateChannelServerInfo(message, INVALID_SOCKET);
        printf("[WorldRegistry] GameServer %u recovered by heartbeat.\n", message.serverId);
        return;
    }

    ChannelServerInfo& info = iterator->second;
    const bool wasInactive = !info.active;
    info.currentSessions = message.currentSessions;
    info.lastHeartbeat = std::chrono::steady_clock::now();
    info.active = true;

    if (wasInactive)
    {
        printf("[WorldRegistry] GameServer %u is active again.\n", message.serverId);
    }
}

void ChannelServerRegistry::Remove(SOCKET socket)
{
    std::lock_guard<std::mutex> lock(mutex);

    for (auto& [serverId, info] : servers)
    {
        if (info.socket == socket)
        {
            info.active = false;
            info.socket = INVALID_SOCKET;
            break;
        }
    }
}

void ChannelServerRegistry::BroadcastWorldChat(const WorldMessage& message)
{
    std::lock_guard<std::mutex> lock(mutex);

    for (auto& [serverId, info] : servers)
    {
        if (!info.active || info.socket == INVALID_SOCKET || info.worldId != message.worldId)
        {
            continue;
        }

        int sentSize = 0;

        while (sentSize < sizeof(message))
        {
            const int result = send(info.socket,
                                    reinterpret_cast<const char*>(&message) + sentSize,
                                    sizeof(message) - sentSize,
                                    0);

            if (result <= 0)
            {
                info.active = false;
                info.socket = INVALID_SOCKET;
                break;
            }

            sentSize += result;
        }
    }
}

void ChannelServerRegistry::MarkTimedOut(std::chrono::seconds timeout)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto now = std::chrono::steady_clock::now();

    for (auto& [serverId, info] : servers)
    {
        if (info.active && now - info.lastHeartbeat > timeout)
        {
            info.active = false;
            printf("[WorldRegistry] GameServer %u timed out and is now inactive.\n", serverId);
        }
    }
}
