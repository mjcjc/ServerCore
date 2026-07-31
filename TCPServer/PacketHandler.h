#pragma once
#include "Session.h"
#include "NetworkCore.h"
#include "PacketHeader.h"
#include "PacketType.h"
#include <atomic>
#include <string>

class Session;
struct LoginRequest;

class PacketHandler
{
public:
    static void HandlePacket(
        Session* session,
        const char* packet
    );

private:
    static void HandleLogin(
        Session* session,
        const LoginRequest* packet
    );

    static void HandleChat(
        Session* session,
        const ChatRequest* packet
    );
};