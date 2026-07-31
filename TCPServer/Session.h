#pragma once
#include"NetworkCore.h"
#include "SendQueue.h"
#include"../Common.h"

class NetworkCore;

enum class SessionState
{
    CONNECTED,
    LOGIN,
    DISCONNECTED
};

class Session
{

public:
    Session(SOCKET client_sock, sockaddr_in clientaddr, NetworkCore* server);

	SOCKET GetSocket() const { return client_sock; }
    NetworkCore* GetServer() const { return server; }
    SessionState GetState() const { return state; }
    const int GetId() const { return userId; }
    const std::string& GetNickname() const { return nickname; }
    
    void SetNickname(const std::string& newNickname)
    {
        nickname = newNickname;
    }
    void SetUserId(const int& userid)
    {
        userId = userid;
    }
    void SetState(const SessionState setstate)
    {
        state = setstate;
    }
    
    void Process();
    bool Send(const char* packetData, int packetSize);
    bool FlushSendQueue();

    void Disconnect();

private:
    SOCKET client_sock;
    sockaddr_in clientaddr;
    char buf[BUFSIZE + 1];
    NetworkCore* server;

    std::mutex sendMutex;

    SendQueue sendQueue;

	SessionState state = SessionState::CONNECTED;

    int userId = 0;
    std::string nickname;
};

