#pragma once
#include"../Common.h"

enum class PacketType : uint16_t
{
    MESSAGE = 1,
    LOGIN_REQUEST = 2,
    LOGIN_RESPONSE = 3,
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