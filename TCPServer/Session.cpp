#include "Session.h"
#include "NetworkCore.h"
#include "SocketUtils.h"
#include "PacketHeader.h"
#include "PacketType.h"
#include <atomic>

std::atomic<int> UserIdCount = 1;

Session::Session(SOCKET client_sock, sockaddr_in clientaddr, NetworkCore* server)
    : client_sock(client_sock), clientaddr(clientaddr), server(server)
{
}
void Session::Process()
{
    while (true)
    {
		PacketHeader header;

		if (!SocketUtils::RecvAll(client_sock, (char*)&header, sizeof(header)))
			break;

		if (header.size < sizeof(PacketHeader))
			break;

		if (header.size > BUFSIZE)
			break;

		int bodySize = header.size - sizeof(PacketHeader);
		char packet[BUFSIZE + 1] = {};

		memcpy(packet, &header, sizeof(header));

		if (!SocketUtils::RecvAll(client_sock, packet + sizeof(PacketHeader), bodySize))
			break;

		char* body = packet + sizeof(PacketHeader);
		body[bodySize] = '\0';


        switch (static_cast<PacketType>(header.type))
        {
        case PacketType::LOGIN_REQUEST:
        {
            if (state != SessionState::CONNECTED)
            {
                printf("[로그인 실패] 이미 로그인된 세션입니다.\n");
                break;
            }

            if (header.size != sizeof(LoginRequest))
            {
                printf("[로그인 실패] 잘못된 패킷 크기입니다.\n");
                break;
            }

            const LoginRequest* loginPacket = reinterpret_cast<const LoginRequest*>(packet);
            size_t nicknameLength = strnlen_s(loginPacket->nickname, sizeof(loginPacket->nickname));
            LoginResponse response{};

            response.header.size = static_cast<uint16_t>(sizeof(LoginResponse));
            response.header.type = static_cast<uint16_t>(PacketType::LOGIN_RESPONSE);

            if (nicknameLength == 0 || nicknameLength >= sizeof(loginPacket->nickname))
            {
                response.success = 0;
                response.userId = 0;

                Send(
                    reinterpret_cast<const char*>(&response),
                    sizeof(response)
                );

                printf("[로그인 실패] 잘못된 닉네임입니다.\n");
                break;
            }

            std::string requestedNickname(loginPacket->nickname,nicknameLength);

            if (!server->TrySetNickname(client_sock,requestedNickname))
            {
                response.success = 0;
                response.userId = 0;

                Send(reinterpret_cast<const char*>(&response),sizeof(response));

                printf("[로그인 실패] 이미 사용 중인 닉네임: %s\n", requestedNickname.c_str());

                break;
            }

            userId = UserIdCount.fetch_add(1);
            state = SessionState::LOGIN;

            response.success = 1;
            response.userId = userId;

            if (!Send(reinterpret_cast<const char*>(&response), sizeof(response)))
            {
                printf("[오류] 로그인 응답 전송 실패\n");
                break;
            }

            printf("[로그인 성공] ID=%d, 닉네임=%s\n",userId, nickname.c_str()
            );

            break;
        }

        case PacketType::MESSAGE:
        {
            if (state != SessionState::LOGIN)
            {
                printf("[오류] 로그인하지 않은 세션의 채팅 요청\n");
                break;
            }
            printf("[받은 채팅] %s: %s\n", nickname.c_str(), body);
            server->Broadcast(client_sock, packet, header.size);

            break;
        }

        default:
        {
            printf(
                "[오류] 알 수 없는 패킷 타입: %d\n",
                header.type
            );

            break;
        }
        }
    }

	server->RemoveClient(client_sock);
    closesocket(client_sock);
    char addr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));

    printf("[TCP 서버] 클라이언트 종료: IP 주소=%s, 포트 번호=%d\n",
        addr, ntohs(clientaddr.sin_port));

}

bool Session::Send(const char* packetData, int packetSize)
{
	std::lock_guard<std::mutex> lock(sendMutex);

	return SocketUtils::SendAll(
		client_sock,
		packetData,
		packetSize
	);
}
