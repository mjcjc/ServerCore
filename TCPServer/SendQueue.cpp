#include "SendQueue.h"

EnqueueResult SendQueue::Enqueue(
    const char* packetData,
    std::size_t packetSize)
{
    if (packetData == nullptr ||
        packetSize == 0)
    {
        return EnqueueResult::REJECTED;
    }

    std::lock_guard<std::mutex> lock(
        queueMutex
    );

    if (isClosed)
    {
        return EnqueueResult::REJECTED;
    }

    packets.emplace(
        packetData,
        packetData + packetSize
    );

    if (isFlushing)
    {
        return EnqueueResult::QUEUED;
    }

    isFlushing = true;

    return EnqueueResult::START_FLUSHING;
}

bool SendQueue::TryPop(
    std::vector<char>& outPacket)
{
    std::lock_guard<std::mutex> lock(
        queueMutex
    );

    if (isClosed || packets.empty())
    {
        isFlushing = false;
        return false;
    }

    outPacket = std::move(
        packets.front()
    );

    packets.pop();

    return true;
}

void SendQueue::Close()
{
    std::lock_guard<std::mutex> lock(
        queueMutex
    );

    isClosed = true;

    while (!packets.empty())
    {
        packets.pop();
    }

    isFlushing = false;
}