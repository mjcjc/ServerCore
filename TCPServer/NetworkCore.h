#pragma once

#include "../Common.h"
#include "IocpCore.h"
#include "ServerConfig.h"

#include <memory>
#include <mutex>
#include <vector>

class Session;
class WorldClient;

class NetworkCore
{
  public:
    explicit NetworkCore(const ServerConfig& config);
    void Initialize();
    void listenSock();
    void bindSock();
    void Run();
    void closeSock();

  public:
    void AddClient(const std::shared_ptr<Session>& session);
    void RemoveClient(const Session* targetSession);
    void Broadcast(const Session* senderSession, const char* packetData, int packetSize);
    void SetWorldClient(WorldClient* client);
    bool SendWorldChat(std::int32_t userId, const std::string& nickname, const char* message);
    void BroadcastWorldChat(std::int32_t userId, std::uint32_t channelId,
                            const char* nickname, const char* message);

    SOCKET AcceptClient(sockaddr_in& clientaddr);
    SOCKET listen_sock;
    sockaddr_in serveraddr;
    sockaddr_in clientaddr;

    bool TrySetNickname(Session* senderSession, const std::string& nickname);
    std::uint32_t GetSessionCount() const;

    int retval;

  private:
    ServerConfig config;
    IocpCore iocpCore;
    std::vector<std::shared_ptr<Session>> sessions;
    mutable std::mutex mtx;
    WorldClient* worldClient = nullptr;
};
