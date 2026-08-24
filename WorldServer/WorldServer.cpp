#include "../MasterServer/MasterProtocol.h"
#include "ChannelServerRegistry.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace
{
    constexpr std::uint32_t SERVER_ID = 1;
    constexpr std::uint32_t WORLD_ID = 1;
    constexpr std::uint16_t WORLD_PORT = 6501;
    constexpr std::uint16_t MASTER_PORT = 6000;
    constexpr char BIND_IP[] = "127.0.0.1";
    constexpr char MASTER_IP[] = "127.0.0.1";
    constexpr std::chrono::seconds HEARTBEAT_TIMEOUT{5};

    bool ReceiveAll(SOCKET socket, char* buffer, int size)
    {
        int receivedSize = 0;

        while (receivedSize < size)
        {
            const int result = recv(socket, buffer + receivedSize, size - receivedSize, 0);

            if (result <= 0)
            {
                return false;
            }

            receivedSize += result;
        }

        return true;
    }

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

    SOCKET ConnectToMaster()
    {
        const SOCKET masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (masterSocket == INVALID_SOCKET)
        {
            return INVALID_SOCKET;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(MASTER_PORT);

        if (inet_pton(AF_INET, MASTER_IP, &address.sin_addr) != 1 ||
            connect(masterSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
                SOCKET_ERROR)
        {
            closesocket(masterSocket);
            return INVALID_SOCKET;
        }

        return masterSocket;
    }

    void RunMasterClient()
    {
        while (true)
        {
            const SOCKET masterSocket = ConnectToMaster();

            if (masterSocket == INVALID_SOCKET)
            {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            MasterMessage message;
            message.type =
                static_cast<std::uint16_t>(MasterMessageType::REGISTER_WORLD_SERVER);
            message.serverId = SERVER_ID;
            message.worldId = WORLD_ID;
            strncpy_s(message.ip, BIND_IP, _TRUNCATE);
            message.port = WORLD_PORT;

            if (!SendAll(masterSocket, reinterpret_cast<const char*>(&message), sizeof(message)))
            {
                closesocket(masterSocket);
                continue;
            }

            printf("[WorldServer %u] Registered with MasterServer. World=%u, Endpoint=%s:%u\n",
                   SERVER_ID,
                   WORLD_ID,
                   BIND_IP,
                   WORLD_PORT);

            while (true)
            {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                message.type =
                    static_cast<std::uint16_t>(MasterMessageType::WORLD_HEARTBEAT);

                if (!SendAll(masterSocket,
                             reinterpret_cast<const char*>(&message),
                             sizeof(message)))
                {
                    printf("[WorldServer %u] MasterServer connection lost. Retrying...\n",
                           SERVER_ID);
                    break;
                }
            }

            closesocket(masterSocket);
        }
    }

    void HandleChannelServer(SOCKET socket, ChannelServerRegistry& registry)
    {
        WorldMessage message;

        while (ReceiveAll(socket, reinterpret_cast<char*>(&message), sizeof(message)))
        {
            if (message.size != sizeof(WorldMessage) || message.worldId != WORLD_ID)
            {
                break;
            }

            switch (static_cast<WorldMessageType>(message.type))
            {
            case WorldMessageType::REGISTER_CHANNEL_SERVER:
                registry.Register(message, socket);
                break;

            case WorldMessageType::WORLD_CHAT:
                registry.BroadcastWorldChat(message);
                break;

            case WorldMessageType::CHANNEL_HEARTBEAT:
                registry.UpdateHeartbeat(message);
                break;

            default:
                printf("[WorldServer] Unknown channel message: %u\n", message.type);
                break;
            }
        }

        registry.Remove(socket);
        closesocket(socket);
    }
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    WSADATA winsockData;

    if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0)
    {
        printf("[WorldServer] WSAStartup failed.\n");
        return 1;
    }

    const SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (listenSocket == INVALID_SOCKET)
    {
        printf("[WorldServer] Failed to create listen socket. Error=%d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(WORLD_PORT);
    inet_pton(AF_INET, BIND_IP, &address.sin_addr);

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("[WorldServer] Failed to listen on %s:%u. Error=%d\n",
               BIND_IP,
               WORLD_PORT,
               WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::thread masterClient(RunMasterClient);
    masterClient.detach();

    ChannelServerRegistry channelRegistry;
    std::thread monitor([&channelRegistry]()
    {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            channelRegistry.MarkTimedOut(HEARTBEAT_TIMEOUT);
        }
    });
    monitor.detach();

    printf("[WorldServer %u] Started. World=%u, Bind=%s, Port=%u\n",
           SERVER_ID,
           WORLD_ID,
           BIND_IP,
           WORLD_PORT);

    while (true)
    {
        const SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);

        if (clientSocket == INVALID_SOCKET)
        {
            continue;
        }

        std::thread(HandleChannelServer, clientSocket, std::ref(channelRegistry)).detach();
    }
}
