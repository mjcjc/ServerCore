#include "GameServerRegistry.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace
{
    constexpr std::uint16_t MASTER_PORT = 6000;
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

    void HandleGameServer(SOCKET socket, GameServerRegistry& registry)
    {
        MasterMessage message;

        while (ReceiveAll(socket, reinterpret_cast<char*>(&message), sizeof(message)))
        {
            if (message.size != sizeof(MasterMessage))
            {
                break;
            }

            switch (static_cast<MasterMessageType>(message.type))
            {
            case MasterMessageType::REGISTER_GAME_SERVER:
                registry.Register(message);
                break;

            case MasterMessageType::HEARTBEAT:
                registry.UpdateHeartbeat(message);
                break;

            case MasterMessageType::ALLOCATION_REQUEST:
            {
                MasterMessage response;
                response.type =
                    static_cast<std::uint16_t>(MasterMessageType::ALLOCATION_RESPONSE);
                const auto selected = registry.SelectLeastLoaded();

                if (selected)
                {
                    response.success = 1;
                    response.serverId = selected->serverId;
                    strncpy_s(response.ip, selected->ip.c_str(), _TRUNCATE);
                    response.port = selected->port;
                    response.currentSessions = selected->currentSessions;
                    response.maxSessions = selected->maxSessions;

                    printf("[Allocator] Assigned GameServer %u (%s:%u, Sessions=%u/%u).\n",
                           response.serverId,
                           response.ip,
                           response.port,
                           response.currentSessions,
                           response.maxSessions);
                }
                else
                {
                    printf("[Allocator] Allocation failed: no available GameServer.\n");
                }

                SendAll(socket, reinterpret_cast<const char*>(&response), sizeof(response));
                closesocket(socket);
                return;
            }

            default:
                printf("[MasterServer] Unknown management message: %u\n", message.type);
                break;
            }
        }

        closesocket(socket);
    }
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    WSADATA winsockData;

    if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0)
    {
        printf("[MasterServer] WSAStartup failed.\n");
        return 1;
    }

    const SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (listenSocket == INVALID_SOCKET)
    {
        printf("[MasterServer] Failed to create listen socket. Error=%d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(MASTER_PORT);

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("[MasterServer] Failed to listen on 127.0.0.1:%u. Error=%d\n",
               MASTER_PORT,
               WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    GameServerRegistry registry;
    std::atomic<bool> running{true};
    std::thread monitor([&registry, &running]()
    {
        while (running.load())
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            registry.MarkTimedOut(HEARTBEAT_TIMEOUT);
        }
    });

    printf("[MasterServer] Started. Bind=127.0.0.1, Port=%u\n", MASTER_PORT);

    while (true)
    {
        const SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);

        if (clientSocket == INVALID_SOCKET)
        {
            printf("[MasterServer] Accept failed. Error=%d\n", WSAGetLastError());
            break;
        }

        std::thread(HandleGameServer, clientSocket, std::ref(registry)).detach();
    }

    running.store(false);
    monitor.join();
    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
