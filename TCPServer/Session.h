#pragma once
#include "IocpEvent.h"
#include "SendQueue.h"
#include "../Common.h"

#include <memory>
#include <vector>

class NetworkCore;

enum class SessionState
{
    CONNECTED,
    LOGIN,
    DISCONNECTED
};

class Session : public std::enable_shared_from_this<Session>
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
    
    bool StartAsyncReceive();
    void HandleIoCompleted(
        IocpEvent* event,
        DWORD transferredBytes,
        DWORD errorCode
    );

    bool Send(const char* packetData, int packetSize);

    void Disconnect();

private:
    bool PostRecv();
    bool HandleRecvCompleted(
        IocpEvent* event,
        DWORD transferredBytes
    );
    bool ProcessReceivedPackets();
    bool StartNextSend();
    bool PostSend(
        std::vector<char> packet,
        std::size_t sendOffset = 0
    );
    bool HandleSendCompleted(
        IocpEvent* event,
        DWORD transferredBytes
    );

    SOCKET client_sock;
    sockaddr_in clientaddr;
    NetworkCore* server;

    std::vector<char> receiveBuffer;

    std::mutex sendMutex;

    SendQueue sendQueue;

	SessionState state = SessionState::CONNECTED;

    int userId = 0;
    std::string nickname;
};
