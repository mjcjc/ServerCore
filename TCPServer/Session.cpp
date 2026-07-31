#include "Session.h"
#include "NetworkCore.h"
#include "SocketUtils.h"
#include "PacketHandler.h"
#include "PacketHeader.h"


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

		PacketHandler::HandlePacket(this, packet);
    }

	Disconnect();
    char addr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));

    printf("[TCP 서버] 클라이언트 종료: IP 주소=%s, 포트 번호=%d\n",
        addr, ntohs(clientaddr.sin_port));

}

bool Session::Send(
    const char* packetData,
    int packetSize)
{
    if (packetData == nullptr ||
        packetSize <= 0)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(sendMutex);

        if (state == SessionState::DISCONNECTED)
        {
            return false;
        }
    }
    EnqueueResult result =
        sendQueue.Enqueue(
            packetData,
            static_cast<std::size_t>(packetSize)
        );

    switch (result)
    {
    case EnqueueResult::REJECTED:
    {
        return false;
    }

    case EnqueueResult::QUEUED:
    {
        return true;
    }

    case EnqueueResult::START_FLUSHING:
    {
        return FlushSendQueue();
    }
    }

    return false;
}
void Session::Disconnect()
{
	SOCKET closesock = INVALID_SOCKET;
	{
		std::lock_guard<std::mutex> lock(sendMutex);
		
		if (state == SessionState::DISCONNECTED)
		{
			return;
		}

		state = SessionState::DISCONNECTED;

        sendQueue.Close();
		closesock = client_sock;
		client_sock = INVALID_SOCKET;
	}
	server->RemoveClient(this);

	if (closesock != INVALID_SOCKET)
	{

		shutdown(closesock, SD_BOTH);
		closesocket(closesock);
	}
}
bool Session::FlushSendQueue()
{
    std::vector<char> packet;

    while (sendQueue.TryPop(packet))
    {
        bool sendSucceeded = false;

        {
            std::lock_guard<std::mutex> lock(
                sendMutex
            );

            if (state != SessionState::DISCONNECTED &&
                client_sock != INVALID_SOCKET)
            {
                sendSucceeded =
                    SocketUtils::SendAll(
                        client_sock,
                        packet.data(),
                        static_cast<int>(
                            packet.size()
                            )
                    );
            }
        }

        if (!sendSucceeded)
        {
            Disconnect();
            return false;
        }

        packet.clear();
    }

    return true;
}