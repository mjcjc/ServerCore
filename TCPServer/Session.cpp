#include "Session.h"
#include "NetworkCore.h"
#include "SocketUtils.h"
#include "PacketHeader.h"
#include "PacketType.h"

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
		char packet[BUFSIZE] = {};

		memcpy(packet, &header, sizeof(header));

		if (!SocketUtils::RecvAll(client_sock, packet + sizeof(PacketHeader), bodySize))
			break;

		char* body = packet + sizeof(PacketHeader);
		body[bodySize] = '\0';

		if (header.type == (uint16_t)PacketType::MESSAGE)
		{
			printf("[받은 채팅] %s\n", body);
			server->Broadcast(client_sock, packet, header.size);
		}
    }

    closesocket(client_sock);
    char addr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));

    printf("[TCP 서버] 클라이언트 종료: IP 주소=%s, 포트 번호=%d\n",
        addr, ntohs(clientaddr.sin_port));

}
