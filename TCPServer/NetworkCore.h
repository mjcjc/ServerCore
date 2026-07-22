#pragma once

#include"../Common.h"
#include<vector>
#include<mutex>
#include"Session.h"


class NetworkCore
{
public:
	void Initialize();
	void listenSock();
	void bindSock();
	void Run();
	void closeSock();


public:
	void AddClient(SOCKET client_sock);
	void RemoveClient(SOCKET client_sock);
	void Broadcast(SOCKET sender_sock, const char* packetData, int packetSize);

	SOCKET AcceptClient(sockaddr_in& clientaddr);
	SOCKET listen_sock;
	SOCKET client_sock;
	sockaddr_in serveraddr;
	sockaddr_in clientaddr;
	int retval;
private:
	std::vector<SOCKET> sessions;
	std::mutex mtx;
};

