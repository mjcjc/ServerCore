#pragma once

#include"../Common.h"
#include<vector>
#include<mutex>
#include"Session.h"

class Session;

class NetworkCore
{
public:
	void Initialize();
	void listenSock();
	void bindSock();
	void Run();
	void closeSock();


public:
	void AddClient(const std::shared_ptr<Session>& session);
	void RemoveClient(SOCKET client_sock);
	void Broadcast(SOCKET sender_sock, const char* packetData, int packetSize);

	SOCKET AcceptClient(sockaddr_in& clientaddr);
	SOCKET listen_sock;
	SOCKET client_sock;
	sockaddr_in serveraddr;
	sockaddr_in clientaddr;

	bool TrySetNickname(SOCKET client_sock, const std::string& nickname);

	int retval;
private:
	std::vector<std::shared_ptr<Session>> sessions;
	std::mutex mtx;
};

