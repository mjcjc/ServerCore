#pragma once

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

    static void HandleMessage(
        Session* session,
        const char* packet
    );
};