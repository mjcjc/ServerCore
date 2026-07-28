#include "NetworkCore.h"
#include "SocketUtils.h"
#include "Session.h"

void NetworkCore::Initialize()
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return;

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET) err_quit("socket()");
}

void NetworkCore::listenSock()
{
	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR) err_quit("listen()");
}

void NetworkCore::bindSock()
{
	serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVERPORT);
	retval = bind(listen_sock, (sockaddr*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR) err_quit("bind()");
}

void NetworkCore::Run()
{
	while (true)
	{
		sockaddr_in clientaddr{};
		SOCKET client_sock = AcceptClient(clientaddr);
		if (client_sock == INVALID_SOCKET)
			continue;
		auto session = std::make_shared<Session>(client_sock, clientaddr, this);
		AddClient(session);
		std::thread t([session]() {
			session->Process();
			});
		t.detach();
	}
}

void NetworkCore::closeSock()
{
	RemoveClient(client_sock);
	
	closesocket(client_sock);
	WSACleanup();
}

SOCKET NetworkCore::AcceptClient(sockaddr_in& clientaddr)
{
	int addrlen = sizeof(clientaddr);

	SOCKET client_sock = accept(
		listen_sock,
		(sockaddr*)&clientaddr,
		&addrlen
	);

	if (client_sock == INVALID_SOCKET)
	{
		return INVALID_SOCKET;
	}

	char addr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));

	printf("\n[TCP 서버] 클라이언트 접속: IP 주소=%s, 포트 번호=%d\n",
		addr, ntohs(clientaddr.sin_port));

	return client_sock;

}

bool NetworkCore::TrySetNickname(SOCKET client_sock, const std::string& nickname)
{
	std::lock_guard<std::mutex> lock(mtx);

	for (const auto& session : sessions)
	{
		if (session->GetSocket() == client_sock)
		{
			continue;
		}

		if (session->GetNickname() == nickname)
		{
			return false;
		}
	}

	for (const auto& session : sessions)
	{
		if (session->GetSocket() == client_sock)
		{
			session->SetNickname(nickname);
			return true;
		}
	}

	return false;
}

void NetworkCore::AddClient(const std::shared_ptr<Session>& session)
{	
	std::lock_guard<std::mutex> lock(mtx);
	sessions.push_back(session);
}

void NetworkCore::RemoveClient(SOCKET client_sock)
{
	std::lock_guard<std::mutex> lock(mtx);
	sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
		[client_sock](const std::shared_ptr<Session>& session) {
			return session->GetSocket() == client_sock;
		}
		),
		sessions.end()
	);
}

void NetworkCore::Broadcast(SOCKET sender_sock, const char* packetData, int packetSize)
{
	std::vector<std::shared_ptr<Session>> sessionsCopy;
	{
		std::lock_guard<std::mutex> lock(mtx);
		sessionsCopy = sessions;
	}
	for (const auto& session : sessionsCopy)
	{
		if (sender_sock == session->GetSocket())
			continue;

		if (!session->Send(packetData, packetSize))
		{
			err_display("SendAll()");
		}
	}
}
