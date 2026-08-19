#include "..\Common.h"
#include <atomic>
#include "SocketUtils.h"
#include "PacketHeader.h"
#include "PacketType.h"
#include "../MasterServer/MasterProtocol.h"

#include <string>

#define BUFSIZE 512

namespace
{
    constexpr std::uint16_t MASTER_PORT = 6000;

    bool RequestGameServer(const char* masterIp, std::string& gameServerIp,
                           std::uint16_t& gameServerPort, std::uint32_t& gameServerId)
    {
        const SOCKET masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (masterSocket == INVALID_SOCKET)
        {
            return false;
        }

        sockaddr_in masterAddress{};
        masterAddress.sin_family = AF_INET;
        masterAddress.sin_port = htons(MASTER_PORT);

        if (inet_pton(AF_INET, masterIp, &masterAddress.sin_addr) != 1 ||
            connect(masterSocket,
                    reinterpret_cast<sockaddr*>(&masterAddress),
                    sizeof(masterAddress)) == SOCKET_ERROR)
        {
            closesocket(masterSocket);
            return false;
        }

        MasterMessage request;
        request.type = static_cast<std::uint16_t>(MasterMessageType::ALLOCATION_REQUEST);

        if (!SocketUtils::SendAll(
                masterSocket, reinterpret_cast<const char*>(&request), sizeof(request)))
        {
            closesocket(masterSocket);
            return false;
        }

        MasterMessage response;
        const bool received = SocketUtils::RecvAll(
            masterSocket, reinterpret_cast<char*>(&response), sizeof(response));
        closesocket(masterSocket);

        if (!received || response.size != sizeof(MasterMessage) ||
            response.type !=
                static_cast<std::uint16_t>(MasterMessageType::ALLOCATION_RESPONSE) ||
            response.success == 0)
        {
            return false;
        }

        gameServerIp = response.ip;
        gameServerPort = response.port;
        gameServerId = response.serverId;
        return true;
    }
}

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    int retval;

    const char* masterIp = argc > 1 ? argv[1] : "127.0.0.1";

    // 윈속 초기화
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        return 1;
    }

    std::string gameServerIp;
    std::uint16_t gameServerPort = 0;
    std::uint32_t gameServerId = 0;

    if (!RequestGameServer(masterIp, gameServerIp, gameServerPort, gameServerId))
    {
        printf("[Allocator] Failed to receive an available GameServer from %s:%u.\n",
               masterIp,
               MASTER_PORT);
        WSACleanup();
        return 1;
    }

    printf("[Allocator] Assigned GameServer %u at %s:%u.\n",
           gameServerId,
           gameServerIp.c_str(),
           gameServerPort);

    // 소켓 생성
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
    {
        err_quit("socket()");
    }

    // connect()
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    inet_pton(AF_INET, gameServerIp.c_str(), &serveraddr.sin_addr);
    serveraddr.sin_port = htons(gameServerPort);

    retval = connect(sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
    if (retval == SOCKET_ERROR)
    {
        err_quit("connect()");
    }
    LoginRequest loginRequest{};

    loginRequest.header.size = static_cast<uint16_t>(sizeof(LoginRequest));

    loginRequest.header.type = static_cast<uint16_t>(PacketType::LOGIN_REQUEST);

    printf("닉네임 입력: ");

    if (fgets(loginRequest.nickname, sizeof(loginRequest.nickname), stdin) == nullptr)
    {
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    size_t nicknameLength = strlen(loginRequest.nickname);

    if (nicknameLength > 0 && loginRequest.nickname[nicknameLength - 1] == '\n')
    {
        loginRequest.nickname[nicknameLength - 1] = '\0';
    }

    if (strlen(loginRequest.nickname) == 0)
    {
        printf("[오류] 닉네임이 비어 있습니다.\n");

        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (!SocketUtils::SendAll(
            sock, reinterpret_cast<const char*>(&loginRequest), sizeof(loginRequest)))
    {
        err_quit("로그인 요청 전송");
    }
    LoginResponse loginResponse{};

    if (!SocketUtils::RecvAll(
            sock, reinterpret_cast<char*>(&loginResponse.header), sizeof(PacketHeader)))
    {
        printf("[오류] 로그인 응답 헤더 수신 실패\n");

        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (loginResponse.header.type != static_cast<uint16_t>(PacketType::LOGIN_RESPONSE))
    {
        printf("[오류] 잘못된 로그인 응답 타입: %d\n", loginResponse.header.type);

        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (loginResponse.header.size != sizeof(LoginResponse))
    {
        printf("[오류] 잘못된 로그인 응답 크기: %d\n", loginResponse.header.size);

        closesocket(sock);
        WSACleanup();
        return 1;
    }

    int responseBodySize = loginResponse.header.size - sizeof(PacketHeader);

    if (!SocketUtils::RecvAll(
            sock, reinterpret_cast<char*>(&loginResponse) + sizeof(PacketHeader), responseBodySize))
    {
        printf("[오류] 로그인 응답 바디 수신 실패\n");

        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (loginResponse.success == 0)
    {
        printf("[로그인 실패] 이미 사용 중이거나 잘못된 닉네임입니다.\n");

        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("[로그인 성공] 발급된 User ID: %d\n", loginResponse.userId);
    // 데이터 통신에 사용할 변수
    char buf[BUFSIZE + 1];

    std::atomic<bool> running = true;

    // 수신 스레드: 서버에서 오는 데이터를 계속 받음
    std::thread recv_thread(
        [&]()
        {
            while (running)
            {
                PacketHeader header{};

                // 헤더 수신
                if (!SocketUtils::RecvAll(
                        sock, reinterpret_cast<char*>(&header), sizeof(PacketHeader)))
                {
                    break;
                }

                // 패킷 크기 검사
                if (header.size < sizeof(PacketHeader))
                {
                    printf("\n[오류] 잘못된 패킷 크기입니다.\n");
                    break;
                }

                if (header.size > BUFSIZE)
                {
                    printf("\n[오류] 패킷 크기가 너무 큽니다.\n");
                    break;
                }

                int bodySize = header.size - sizeof(PacketHeader);

                // 헤더와 바디를 합쳐서 저장할 전체 패킷 버퍼
                char packet[BUFSIZE + 1]{};

                memcpy(packet, &header, sizeof(PacketHeader));

                // 바디 수신
                if (!SocketUtils::RecvAll(sock, packet + sizeof(PacketHeader), bodySize))
                {
                    break;
                }

                switch (static_cast<PacketType>(header.type))
                {
                case PacketType::CHAT_RESPONSE:
                {
                    if (header.size != sizeof(ChatResponse))
                    {
                        printf("\n[오류] 잘못된 채팅 응답 크기입니다. "
                               "size=%d\n",
                               header.size);

                        break;
                    }

                    const ChatResponse* response = reinterpret_cast<const ChatResponse*>(packet);

                    size_t nicknameLength =
                        strnlen_s(response->nickname, sizeof(response->nickname));

                    size_t messageLength = strnlen_s(response->message, sizeof(response->message));

                    // 배열 내부에 널 문자가 없는 잘못된 패킷
                    if (nicknameLength >= sizeof(response->nickname) ||
                        messageLength >= sizeof(response->message))
                    {
                        printf("\n[오류] 잘못된 채팅 응답입니다.\n");

                        break;
                    }

                    printf("\n[%s / ID:%d] %s\n",
                           response->nickname,
                           response->userId,
                           response->message);

                    printf("[보낼 데이터] ");
                    fflush(stdout);

                    break;
                }

                default:
                {
                    printf("\n[오류] 알 수 없는 패킷 타입입니다. "
                           "type=%d\n",
                           header.type);

                    break;
                }
                }
            }

            running = false;
        });
    // 송신 루프: 키보드 입력을 받아 서버로 보냄
    while (running)
    {
        printf("\n[보낼 데이터] ");

        if (fgets(buf, BUFSIZE + 1, stdin) == nullptr)
        {
            break;
        }

        size_t messageLength = strlen(buf);

        // fgets()가 입력한 마지막 개행 문자 제거
        if (messageLength > 0 && buf[messageLength - 1] == '\n')
        {
            buf[messageLength - 1] = '\0';
            --messageLength;
        }

        // 빈 메시지는 전송하지 않음
        if (messageLength == 0)
        {
            continue;
        }

        ChatRequest request{};

        // message[256]에 들어갈 수 있는지 검사
        if (messageLength >= sizeof(request.message))
        {
            printf("[오류] 메시지는 최대 %zu바이트까지 가능합니다.\n", sizeof(request.message) - 1);

            continue;
        }

        request.header.size = static_cast<uint16_t>(sizeof(ChatRequest));

        request.header.type = static_cast<uint16_t>(PacketType::CHAT_REQUEST);

        strncpy_s(request.message, sizeof(request.message), buf, _TRUNCATE);

        if (!SocketUtils::SendAll(
                sock, reinterpret_cast<const char*>(&request), sizeof(ChatRequest)))
        {
            err_display("ChatRequest SendAll()");
            break;
        }

        printf("[TCP 클라이언트] %zu바이트를 보냈습니다.\n", sizeof(ChatRequest));
    }

    // 종료 처리
    running = false;

    // recv()에서 대기 중인 수신 스레드를 깨우기 위해 shutdown
    shutdown(sock, SD_BOTH);

    if (recv_thread.joinable())
    {
        recv_thread.join();
    }

    // 소켓 닫기
    closesocket(sock);

    // 윈속 종료
    WSACleanup();

    return 0;
}
