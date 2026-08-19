#pragma once

#include <cstdint>

enum class MasterMessageType : std::uint16_t
{
    REGISTER_GAME_SERVER = 1,
    HEARTBEAT = 2,
    ALLOCATION_REQUEST = 3,
    ALLOCATION_RESPONSE = 4
};

#pragma pack(push, 1)
struct MasterMessage
{
    std::uint16_t size = sizeof(MasterMessage);
    std::uint16_t type = 0;
    std::uint32_t serverId = 0;
    char ip[16]{};
    std::uint16_t port = 0;
    std::uint32_t currentSessions = 0;
    std::uint32_t maxSessions = 0;
    std::uint8_t success = 0;
};
#pragma pack(pop)
