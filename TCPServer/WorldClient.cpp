#include "WorldClient.h"

#include "NetworkCore.h"
#include "../WorldServer/WorldProtocol.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <chrono>
#include <cstdio>
#include <cstring>

namespace
{
    bool SendAll(SOCKET socket, const char* buffer, int size)
    {
        int sentSize = 0;

        while (sentSize < size)
        {
            const int result = send(socket, buffer + sentSize, size - sentSize, 0);

            if (result <= 0)
            {
                return false;
            }

            sentSize += result;
        }

        return true;
    }
}

WorldClient::WorldClient(const ServerConfig& config, NetworkCore& server)
    : config(config), server(server)
{
}

WorldClient::~WorldClient()
{
    Stop();
}

void WorldClient::Start()
{
    if (running.exchange(true))
    {
        return;
    }

    worker = std::thread(&WorldClient::Run, this);
}

void WorldClient::Stop()
{
    running.store(false);
    const SOCKET socket = currentSocket.exchange(INVALID_SOCKET);

    if (socket != INVALID_SOCKET)
    {
        shutdown(socket, SD_BOTH);
    }

    if (worker.joinable())
    {
        worker.join();
    }
}

bool WorldClient::SendWorldMessage(const WorldMessage& message)
{
    std::lock_guard<std::mutex> lock(sendMutex);
    const SOCKET socket = currentSocket.load();
    return socket != INVALID_SOCKET &&
           SendAll(socket, reinterpret_cast<const char*>(&message), sizeof(message));
}

bool WorldClient::SendWorldChat(std::int32_t userId, const std::string& nickname,
                                const char* chatMessage)
{
    WorldMessage message;
    message.type = static_cast<std::uint16_t>(WorldMessageType::WORLD_CHAT);
    message.serverId = config.serverId;
    message.worldId = config.worldId;
    message.channelId = config.channelId;
    message.userId = userId;
    strncpy_s(message.nickname, nickname.c_str(), _TRUNCATE);
    strncpy_s(message.message, chatMessage, _TRUNCATE);
    return SendWorldMessage(message);
}

void WorldClient::Run()
{
    while (running.load())
    {
        const SOCKET worldSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (worldSocket == INVALID_SOCKET)
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(config.worldServerPort);

        if (inet_pton(AF_INET, config.worldServerIp.c_str(), &address.sin_addr) != 1 ||
            connect(worldSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
                SOCKET_ERROR)
        {
            closesocket(worldSocket);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        WorldMessage message;
        message.type =
            static_cast<std::uint16_t>(WorldMessageType::REGISTER_CHANNEL_SERVER);
        message.serverId = config.serverId;
        message.worldId = config.worldId;
        message.channelId = config.channelId;
        strncpy_s(message.ip, config.bindIp.c_str(), _TRUNCATE);
        message.port = config.port;
        message.currentSessions = server.GetSessionCount();

        currentSocket.store(worldSocket);

        if (!SendWorldMessage(message))
        {
            currentSocket.store(INVALID_SOCKET);
            closesocket(worldSocket);
            continue;
        }

        printf("[GameServer %u] Registered with WorldServer %s:%u. World=%u, Channel=%u\n",
               config.serverId,
               config.worldServerIp.c_str(),
               config.worldServerPort,
               config.worldId,
               config.channelId);

        auto nextHeartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        while (running.load())
        {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(worldSocket, &readSet);
            timeval timeout{0, 200000};
            const int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);

            if (selectResult == SOCKET_ERROR)
            {
                break;
            }

            if (selectResult > 0 && FD_ISSET(worldSocket, &readSet))
            {
                WorldMessage incoming;

                if (recv(worldSocket, reinterpret_cast<char*>(&incoming), sizeof(incoming), MSG_WAITALL) !=
                    sizeof(incoming))
                {
                    break;
                }

                if (incoming.type == static_cast<std::uint16_t>(WorldMessageType::WORLD_CHAT))
                {
                    server.BroadcastWorldChat(incoming.userId,
                                              incoming.channelId,
                                              incoming.nickname,
                                              incoming.message);
                }
            }

            if (std::chrono::steady_clock::now() >= nextHeartbeat)
            {
                message.type = static_cast<std::uint16_t>(WorldMessageType::CHANNEL_HEARTBEAT);
                message.currentSessions = server.GetSessionCount();

                if (!SendWorldMessage(message))
                {
                    break;
                }

                nextHeartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            }
        }

        if (currentSocket.load() == worldSocket)
        {
            currentSocket.store(INVALID_SOCKET);
        }
        closesocket(worldSocket);

        if (running.load())
        {
            printf("[GameServer %u] WorldServer connection lost. Retrying...\n", config.serverId);
        }
    }
}
