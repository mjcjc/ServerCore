#pragma once

#include <cstddef>
#include <mutex>
#include <queue>
#include <vector>

enum class EnqueueResult
{
    REJECTED,       // 큐가 닫혔거나 잘못된 패킷
    QUEUED,         // 큐에는 넣었지만 다른 스레드가 전송 중
    START_FLUSHING  // 현재 스레드가 전송을 시작해야 함
};

class SendQueue
{
public:
    EnqueueResult Enqueue(
        const char* packetData,
        std::size_t packetSize
    );

    bool TryPop(
        std::vector<char>& outPacket
    );

    void Close();

private:
    std::mutex queueMutex;

    std::queue<std::vector<char>> packets;

    bool isFlushing = false;
    bool isClosed = false;
};