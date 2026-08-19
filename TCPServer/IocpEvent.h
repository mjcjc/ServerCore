#pragma once

#include "../Common.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

class Session;

enum class IoOperation
{
    RECV,
    SEND
};

struct IocpEvent
{
    IocpEvent(IoOperation operation, std::shared_ptr<Session> ownerSession)
        : type(operation), owner(std::move(ownerSession))
    {
        wsabuf.buf = storage.data();
        wsabuf.len = static_cast<ULONG>(storage.size());
    }

    void SetSendData(std::vector<char> packet)
    {
        sendData = std::move(packet);
        sendOffset = 0;
        UpdateSendBuffer();
    }

    void AdvanceSend(DWORD transferredBytes)
    {
        sendOffset += transferredBytes;
        UpdateSendBuffer();
    }

    bool IsSendComplete() const
    {
        return sendOffset >= sendData.size();
    }

  private:
    void UpdateSendBuffer()
    {
        wsabuf.buf = sendData.data() + sendOffset;
        wsabuf.len = static_cast<ULONG>(sendData.size() - sendOffset);
    }

  public:
    OVERLAPPED overlapped{};
    IoOperation type;
    std::shared_ptr<Session> owner;
    std::array<char, BUFSIZE> storage{};
    std::vector<char> sendData;
    std::size_t sendOffset = 0;
    WSABUF wsabuf{};
};
