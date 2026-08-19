#include "NetworkCore.h"
#include "SocketUtils.h"
#include "Session.h"

#include "IocpEvent.h"

#include <memory>

NetworkCore::NetworkCore(const ServerConfig& config)
    : config(config)
{
}

void NetworkCore::Initialize()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        return;
    }

    if (!iocpCore.Start(
            config.workerCount,
            [](DWORD transferredBytes, ULONG_PTR completionKey, OVERLAPPED* overlapped, DWORD errorCode)
            {
                auto event = std::unique_ptr<IocpEvent>(reinterpret_cast<IocpEvent*>(overlapped));

                if (!event || !event->owner)
                {
                    return;
                }

                Session* completedSession = reinterpret_cast<Session*>(completionKey);

                if (completedSession != event->owner.get())
                {
                    event->owner->Disconnect();
                    return;
                }

                event->owner->HandleIoCompleted(event.get(), transferredBytes, errorCode);
            }))
    {
        err_quit("IocpCore::Start()");
    }

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET)
    {
        err_quit("socket()");
    }
}

void NetworkCore::listenSock()
{
    retval = listen(listen_sock, SOMAXCONN);
    if (retval == SOCKET_ERROR)
    {
        err_quit("listen()");
    }
}

void NetworkCore::bindSock()
{
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(config.port);

    if (config.bindIp == "0.0.0.0")
    {
        serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else if (inet_pton(AF_INET, config.bindIp.c_str(), &serveraddr.sin_addr) != 1)
    {
        printf("[GameServer %u] Invalid bind IP: %s\n", config.serverId, config.bindIp.c_str());
        exit(1);
    }

    retval = bind(listen_sock, (sockaddr*)&serveraddr, sizeof(serveraddr));
    if (retval == SOCKET_ERROR)
    {
        err_quit("bind()");
    }
}

void NetworkCore::Run()
{
    while (true)
    {
        sockaddr_in clientaddr{};
        SOCKET client_sock = AcceptClient(clientaddr);
        if (client_sock == INVALID_SOCKET)
        {
            continue;
        }
        auto session = std::make_shared<Session>(client_sock, clientaddr, this);
        AddClient(session);

        if (!iocpCore.RegisterSocket(client_sock, reinterpret_cast<ULONG_PTR>(session.get())))
        {
            session->Disconnect();
            continue;
        }

        if (!session->StartAsyncReceive())
        {
            session->Disconnect();
        }
    }
}

void NetworkCore::closeSock()
{
    iocpCore.Stop();

    if (listen_sock != INVALID_SOCKET)
    {
        closesocket(listen_sock);
        listen_sock = INVALID_SOCKET;
    }
    WSACleanup();
}

SOCKET NetworkCore::AcceptClient(sockaddr_in& clientaddr)
{
    int addrlen = sizeof(clientaddr);

    SOCKET client_sock = accept(listen_sock, (sockaddr*)&clientaddr, &addrlen);

    if (client_sock == INVALID_SOCKET)
    {
        return INVALID_SOCKET;
    }

    char addr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));

    printf("\n[GameServer %u] Client connected: IP=%s, Port=%d\n",
           config.serverId,
           addr,
           ntohs(clientaddr.sin_port));

    return client_sock;
}

bool NetworkCore::TrySetNickname(Session* senderSession, const std::string& nickname)
{
    std::lock_guard<std::mutex> lock(mtx);

    for (const auto& session : sessions)
    {
        if (session.get() == senderSession)
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
        if (session.get() == senderSession)
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

void NetworkCore::RemoveClient(const Session* targetSession)
{
    std::lock_guard<std::mutex> lock(mtx);
    sessions.erase(std::remove_if(sessions.begin(),sessions.end(),
                                  [targetSession](const std::shared_ptr<Session>& session)
                                  {
                                      return session.get() == targetSession;
                                  }),
                   sessions.end());
}

void NetworkCore::Broadcast(const Session* senderSession, const char* packetData, int packetSize)
{
    std::vector<std::shared_ptr<Session>> sessionsCopy;
    {
        std::lock_guard<std::mutex> lock(mtx);
        sessionsCopy = sessions;
    }
    for (const auto& session : sessionsCopy)
    {
        if (senderSession == session.get())
        {
            continue;
        }

        if (!session->Send(packetData, packetSize))
        {
            err_display("Send()");
            session->Disconnect();
        }
    }
}

std::uint32_t NetworkCore::GetSessionCount() const
{
    std::lock_guard<std::mutex> lock(mtx);
    return static_cast<std::uint32_t>(sessions.size());
}
