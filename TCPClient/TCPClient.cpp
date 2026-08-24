#include "..\Common.h"
#include "../MasterServer/MasterProtocol.h"
#include "PacketHeader.h"
#include "PacketType.h"
#include "SocketUtils.h"

#include <atomic>
#include <string>

#define BUFSIZE 512

namespace
{
    constexpr std::uint16_t MASTER_PORT = 6000;

    struct Allocation
    {
        std::string ip;
        std::uint16_t port = 0;
        std::uint32_t serverId = 0;
        std::uint32_t worldId = 0;
        std::uint32_t channelId = 0;
    };

    bool RequestGameServer(const char* masterIp, std::uint32_t channelId, Allocation& allocation)
    {
        const SOCKET masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (masterSocket == INVALID_SOCKET)
        {
            return false;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(MASTER_PORT);

        if (inet_pton(AF_INET, masterIp, &address.sin_addr) != 1 ||
            connect(masterSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            closesocket(masterSocket);
            return false;
        }

        MasterMessage request;
        request.type = static_cast<std::uint16_t>(MasterMessageType::ALLOCATION_REQUEST);
        request.worldId = 1;
        request.channelId = channelId;

        if (!SocketUtils::SendAll(masterSocket, reinterpret_cast<const char*>(&request), sizeof(request)))
        {
            closesocket(masterSocket);
            return false;
        }

        MasterMessage response;
        const bool received = SocketUtils::RecvAll(masterSocket, reinterpret_cast<char*>(&response), sizeof(response));
        closesocket(masterSocket);

        if (!received || response.size != sizeof(response) ||
            response.type != static_cast<std::uint16_t>(MasterMessageType::ALLOCATION_RESPONSE) ||
            response.success == 0)
        {
            return false;
        }

        allocation.ip = response.ip;
        allocation.port = response.port;
        allocation.serverId = response.serverId;
        allocation.worldId = response.worldId;
        allocation.channelId = response.channelId;
        return true;
    }

    SOCKET ConnectAndLogin(const Allocation& allocation, const std::string& nickname)
    {
        const SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);

        if (clientSocket == INVALID_SOCKET)
        {
            return INVALID_SOCKET;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(allocation.port);
        inet_pton(AF_INET, allocation.ip.c_str(), &address.sin_addr);

        if (connect(clientSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            closesocket(clientSocket);
            return INVALID_SOCKET;
        }

        LoginRequest request{};
        request.header.size = static_cast<std::uint16_t>(sizeof(request));
        request.header.type = static_cast<std::uint16_t>(PacketType::LOGIN_REQUEST);
        strncpy_s(request.nickname, nickname.c_str(), _TRUNCATE);
        LoginResponse response{};

        if (!SocketUtils::SendAll(clientSocket, reinterpret_cast<const char*>(&request), sizeof(request)) ||
            !SocketUtils::RecvAll(clientSocket, reinterpret_cast<char*>(&response), sizeof(response)) ||
            response.header.type != static_cast<std::uint16_t>(PacketType::LOGIN_RESPONSE) ||
            response.header.size != sizeof(response) || response.success == 0)
        {
            closesocket(clientSocket);
            return INVALID_SOCKET;
        }

        printf("[로그인 성공] User ID: %d, 월드 %u / 채널 %u\n", response.userId, allocation.worldId, allocation.channelId);
        return clientSocket;
    }

    void ReceivePackets(SOCKET clientSocket, std::atomic<bool>& connected)
    {
        while (connected.load())
        {
            PacketHeader header{};

            if (!SocketUtils::RecvAll(clientSocket, reinterpret_cast<char*>(&header), sizeof(header)) ||
                header.size < sizeof(header) || header.size > BUFSIZE)
            {
                break;
            }

            char packet[BUFSIZE]{};
            memcpy(packet, &header, sizeof(header));

            if (!SocketUtils::RecvAll(clientSocket, packet + sizeof(header), header.size - sizeof(header)))
            {
                break;
            }

            if (header.type == static_cast<std::uint16_t>(PacketType::CHAT_RESPONSE) && header.size == sizeof(ChatResponse))
            {
                const ChatResponse* response = reinterpret_cast<const ChatResponse*>(packet);
                printf("\n[채널] %s(ID:%d): %s\n", response->nickname, response->userId, response->message);
            }
            else if (header.type == static_cast<std::uint16_t>(PacketType::WORLD_CHAT_RESPONSE) &&
                     header.size == sizeof(WorldChatResponse))
            {
                const WorldChatResponse* response = reinterpret_cast<const WorldChatResponse*>(packet);
                printf("\n[월드/채널 %u] %s(ID:%d): %s\n", response->channelId, response->nickname, response->userId, response->message);
            }

            printf("[보낼 데이터] ");
            fflush(stdout);
        }

        connected.store(false);
    }
}

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    WSADATA winsockData;

    if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0)
    {
        return 1;
    }

    const char* masterIp = argc > 1 ? argv[1] : "127.0.0.1";
    char nicknameBuffer[20]{};
    printf("닉네임 입력: ");

    if (fgets(nicknameBuffer, sizeof(nicknameBuffer), stdin) == nullptr)
    {
        WSACleanup();
        return 1;
    }

    nicknameBuffer[strcspn(nicknameBuffer, "\r\n")] = '\0';
    const std::string nickname = nicknameBuffer;
    std::uint32_t requestedChannel = 0;
    bool running = !nickname.empty();

    while (running)
    {
        Allocation allocation;

        if (!RequestGameServer(masterIp, requestedChannel, allocation))
        {
            printf("[배정 실패] 요청한 채널을 사용할 수 없습니다.\n");
            break;
        }

        printf("[배정] GameServer %u, 월드 %u / 채널 %u (%s:%u)\n", allocation.serverId, allocation.worldId, allocation.channelId, allocation.ip.c_str(), allocation.port);
        const SOCKET clientSocket = ConnectAndLogin(allocation, nickname);

        if (clientSocket == INVALID_SOCKET)
        {
            printf("[연결 실패] GameServer에 로그인할 수 없습니다.\n");
            break;
        }

        std::atomic<bool> connected{true};
        std::thread receiver(ReceivePackets, clientSocket, std::ref(connected));
        char input[BUFSIZE + 1]{};
        bool movingChannel = false;

        while (connected.load())
        {
            printf("\n[보낼 데이터] ");

            if (fgets(input, sizeof(input), stdin) == nullptr)
            {
                running = false;
                break;
            }

            input[strcspn(input, "\r\n")] = '\0';

            if (strncmp(input, "/channel ", 9) == 0)
            {
                const int channel = atoi(input + 9);

                if (channel <= 0)
                {
                    printf("[채널 이동] 사용법: /channel 2\n");
                    continue;
                }

                requestedChannel = static_cast<std::uint32_t>(channel);
                movingChannel = true;
                break;
            }

            const bool worldChat = strncmp(input, "/world ", 7) == 0;
            const char* message = worldChat ? input + 7 : input;

            if (*message == '\0' || strlen(message) >= 256)
            {
                continue;
            }

            if (worldChat)
            {
                WorldChatRequest request{};
                request.header.size = static_cast<std::uint16_t>(sizeof(request));
                request.header.type = static_cast<std::uint16_t>(PacketType::WORLD_CHAT_REQUEST);
                strncpy_s(request.message, message, _TRUNCATE);
                connected.store(SocketUtils::SendAll(clientSocket, reinterpret_cast<const char*>(&request), sizeof(request)));
            }
            else
            {
                ChatRequest request{};
                request.header.size = static_cast<std::uint16_t>(sizeof(request));
                request.header.type = static_cast<std::uint16_t>(PacketType::CHAT_REQUEST);
                strncpy_s(request.message, message, _TRUNCATE);
                connected.store(SocketUtils::SendAll(clientSocket, reinterpret_cast<const char*>(&request), sizeof(request)));
            }
        }

        connected.store(false);
        shutdown(clientSocket, SD_BOTH);
        receiver.join();
        closesocket(clientSocket);

        if (!movingChannel)
        {
            running = false;
        }
        else
        {
            printf("[채널 이동] %u채널로 재접속합니다.\n", requestedChannel);
        }
    }

    WSACleanup();
    return 0;
}
