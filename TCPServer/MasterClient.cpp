#include "MasterClient.h"

#include "NetworkCore.h"
#include "../MasterServer/MasterProtocol.h"

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

MasterClient::MasterClient(const ServerConfig& config, const NetworkCore& server)
    : config(config), server(server)
{
}

MasterClient::~MasterClient()
{
    Stop();
}

void MasterClient::Start()
{
    if (running.exchange(true))
    {
        return;
    }

    worker = std::thread(&MasterClient::Run, this);
}

void MasterClient::Stop()
{
    running.store(false);

    if (worker.joinable())
    {
        worker.join();
    }
}

void MasterClient::Run()
{
    while (running.load())
    {
        const SOCKET masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (masterSocket == INVALID_SOCKET)
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(config.masterPort);

        if (inet_pton(AF_INET, config.masterIp.c_str(), &address.sin_addr) != 1 ||
            connect(masterSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            closesocket(masterSocket);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        MasterMessage message;
        message.type = static_cast<std::uint16_t>(MasterMessageType::REGISTER_GAME_SERVER);
        message.serverId = config.serverId;
        message.worldId = config.worldId;
        message.channelId = config.channelId;
        strncpy_s(message.ip, config.bindIp.c_str(), _TRUNCATE);
        message.port = config.port;
        message.currentSessions = server.GetSessionCount();
        message.maxSessions = config.maxSessions;

        if (!SendAll(masterSocket, reinterpret_cast<const char*>(&message), sizeof(message)))
        {
            closesocket(masterSocket);
            continue;
        }

        printf("[GameServer %u] Registered with MasterServer %s:%u.\n",
               config.serverId,
               config.masterIp.c_str(),
               config.masterPort);

        while (running.load())
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            message.type = static_cast<std::uint16_t>(MasterMessageType::HEARTBEAT);
            message.currentSessions = server.GetSessionCount();

            if (!SendAll(masterSocket, reinterpret_cast<const char*>(&message), sizeof(message)))
            {
                printf("[GameServer %u] MasterServer connection lost. Retrying...\n",
                       config.serverId);
                break;
            }
        }

        closesocket(masterSocket);
    }
}
