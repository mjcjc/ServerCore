#pragma once

#include "../Common.h"
#include "IocpCore.h"

#include <memory>
#include <mutex>
#include <vector>

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
	void RemoveClient(const Session* targetSession);
	void Broadcast(const Session* senderSession, const char* packetData, int packetSize);

	SOCKET AcceptClient(sockaddr_in& clientaddr);
	SOCKET listen_sock;
	sockaddr_in serveraddr;
	sockaddr_in clientaddr;

	bool TrySetNickname(Session* senderSession, const std::string& nickname);

	int retval;
private:
	IocpCore iocpCore;
	std::vector<std::shared_ptr<Session>> sessions;
	std::mutex mtx;
};
