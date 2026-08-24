#pragma once
#include "../Common.h"

enum class PacketType : uint16_t
{
    LOGIN_REQUEST = 1,
    LOGIN_RESPONSE = 2,

    CHAT_REQUEST = 3,
    CHAT_RESPONSE = 4,
    WORLD_CHAT_REQUEST = 5,
    WORLD_CHAT_RESPONSE = 6,
};

struct LoginRequest
{
    PacketHeader header;
    char nickname[20];
};

struct LoginResponse
{
    PacketHeader header;
    uint8_t success;
    int userId;
};

struct ChatRequest
{
    PacketHeader header;
    char message[256];
};

struct ChatResponse
{
    PacketHeader header;

    int32_t userId;
    char nickname[20];
    char message[256];
};

struct WorldChatRequest
{
    PacketHeader header;
    char message[256];
};

struct WorldChatResponse
{
    PacketHeader header;
    int32_t userId;
    uint32_t channelId;
    char nickname[20];
    char message[256];
};
