#pragma once

#include <cstdint>

enum class WorldMessageType : std::uint16_t
{
    REGISTER_CHANNEL_SERVER = 1,
    CHANNEL_HEARTBEAT = 2,
    WORLD_CHAT = 3
};

#pragma pack(push, 1)
struct WorldMessage
{
    std::uint16_t size = sizeof(WorldMessage);
    std::uint16_t type = 0;
    std::uint32_t serverId = 0;
    std::uint32_t worldId = 0;
    std::uint32_t channelId = 0;
    char ip[16]{};
    std::uint16_t port = 0;
    std::uint32_t currentSessions = 0;
    std::int32_t userId = 0;
    char nickname[20]{};
    char message[256]{};
};
#pragma pack(pop)
