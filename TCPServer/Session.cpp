#include "Session.h"
#include "NetworkCore.h"
#include "PacketHandler.h"
#include "PacketHeader.h"

#include <cstring>
#include <memory>

Session::Session(SOCKET client_sock, sockaddr_in clientaddr, NetworkCore* server)
    : client_sock(client_sock), clientaddr(clientaddr), server(server)
{
}

bool Session::StartAsyncReceive()
{
    receiveBuffer.reserve(BUFSIZE * 2);
    return PostRecv();
}

bool Session::PostRecv()
{
    SOCKET socket = INVALID_SOCKET;

    {
        std::lock_guard<std::mutex> lock(sendMutex);

        if (state == SessionState::DISCONNECTED || client_sock == INVALID_SOCKET)
        {
            return false;
        }

        socket = client_sock;
    }

    auto event = std::make_unique<IocpEvent>(IoOperation::RECV, shared_from_this());

    DWORD flags = 0;
    DWORD receivedBytes = 0;

    const int result =
        WSARecv(socket, &event->wsabuf, 1, &receivedBytes, &flags, &event->overlapped, nullptr);

    if (result == SOCKET_ERROR)
    {
        const int errorCode = WSAGetLastError();

        if (errorCode != WSA_IO_PENDING)
        {
            Disconnect();
            return false;
        }
    }

    event.release();
    return true;
}

void Session::HandleIoCompleted(IocpEvent* event, DWORD transferredBytes, DWORD errorCode)
{
    if (errorCode != ERROR_SUCCESS || transferredBytes == 0)
    {
        Disconnect();
        return;
    }

    switch (event->type)
    {
    case IoOperation::RECV:
        HandleRecvCompleted(event, transferredBytes);
        break;

    case IoOperation::SEND:
        if (!HandleSendCompleted(event, transferredBytes))
        {
            Disconnect();
        }
        break;
    }
}

bool Session::HandleRecvCompleted(IocpEvent* event, DWORD transferredBytes)
{
    receiveBuffer.insert(
        receiveBuffer.end(), event->storage.data(), event->storage.data() + transferredBytes);

    if (!ProcessReceivedPackets())
    {
        Disconnect();
        return false;
    }

    return PostRecv();
}

bool Session::ProcessReceivedPackets()
{
    while (receiveBuffer.size() >= sizeof(PacketHeader))
    {
        PacketHeader header{};
        std::memcpy(&header, receiveBuffer.data(), sizeof(header));

        if (header.size < sizeof(PacketHeader) || header.size > BUFSIZE)
        {
            return false;
        }

        if (receiveBuffer.size() < header.size)
        {
            break;
        }

        PacketHandler::HandlePacket(this, receiveBuffer.data());

        receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + header.size);

        if (GetState() == SessionState::DISCONNECTED)
        {
            return false;
        }
    }

    return true;
}

bool Session::Send(const char* packetData, int packetSize)
{
    if (packetData == nullptr || packetSize <= 0)
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
    EnqueueResult result = sendQueue.Enqueue(packetData, static_cast<std::size_t>(packetSize));

    switch (result)
    {
    case EnqueueResult::REJECTED:
    {
        return false;
    }
    case EnqueueResult::QUEUE_FULL:
    {
        printf("SendQueue 최대 용량 초과 세션 종료\n");

        Disconnect();
        return false;
    }
    case EnqueueResult::QUEUED:
    {
        return true;
    }

    case EnqueueResult::START_FLUSHING:
    {
        if (!StartNextSend())
        {
            Disconnect();
            return false;
        }

        return true;
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
bool Session::StartNextSend()
{
    std::vector<char> packet;

    if (!sendQueue.TryBeginSend(packet))
    {
        return false;
    }

    return PostSend(std::move(packet));
}

bool Session::PostSend(std::vector<char> packet, std::size_t sendOffset)
{
    SOCKET socket = INVALID_SOCKET;

    {
        std::lock_guard<std::mutex> lock(sendMutex);

        if (state == SessionState::DISCONNECTED || client_sock == INVALID_SOCKET)
        {
            return false;
        }

        socket = client_sock;
    }

    auto event = std::make_unique<IocpEvent>(IoOperation::SEND, shared_from_this());

    event->SetSendData(std::move(packet));

    if (sendOffset > event->sendData.size())
    {
        return false;
    }

    event->AdvanceSend(static_cast<DWORD>(sendOffset));

    if (event->wsabuf.len == 0)
    {
        return false;
    }

    DWORD sentBytes = 0;

    const int result =
        WSASend(socket, &event->wsabuf, 1, &sentBytes, 0, &event->overlapped, nullptr);

    if (result == SOCKET_ERROR)
    {
        const int errorCode = WSAGetLastError();

        if (errorCode != WSA_IO_PENDING)
        {
            return false;
        }
    }

    event.release();
    return true;
}

bool Session::HandleSendCompleted(IocpEvent* event, DWORD transferredBytes)
{
    if (transferredBytes > event->wsabuf.len)
    {
        return false;
    }

    event->AdvanceSend(transferredBytes);

    if (!event->IsSendComplete())
    {
        return PostSend(std::move(event->sendData), event->sendOffset);
    }

    const std::size_t completedPacketSize = event->sendData.size();
    std::vector<char> nextPacket;

    if (!sendQueue.CompleteSendAndGetNext(completedPacketSize, nextPacket))
    {
        return true;
    }

    return PostSend(std::move(nextPacket));
}
