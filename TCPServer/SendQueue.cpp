#include "SendQueue.h"

#include <algorithm>

EnqueueResult SendQueue::Enqueue(const char* packetData, std::size_t packetSize)
{
    if (packetData == nullptr || packetSize == 0)
    {
        return EnqueueResult::REJECTED;
    }

    std::lock_guard<std::mutex> lock(queueMutex);

    if (isClosed)
    {
        return EnqueueResult::REJECTED;
    }
    if (packetSize > MAX_QUEUED_BYTES)
    {
        return EnqueueResult::QUEUE_FULL;
    }

    if (queuedBytes > MAX_QUEUED_BYTES - packetSize)
    {
        return EnqueueResult::QUEUE_FULL;
    }
    packets.emplace(packetData, packetData + packetSize);

    queuedBytes += packetSize;

    if (isFlushing)
    {
        return EnqueueResult::QUEUED;
    }

    isFlushing = true;

    return EnqueueResult::START_FLUSHING;
}

bool SendQueue::TryPop(std::vector<char>& outPacket)
{
    std::lock_guard<std::mutex> lock(queueMutex);

    if (isClosed)
    {
        isFlushing = false;
        return false;
    }

    if (packets.empty())
    {
        isFlushing = false;
        return false;
    }

    std::size_t packetSize = packets.front().size();

    outPacket = std::move(packets.front());

    packets.pop();

    if (queuedBytes >= packetSize)
    {
        queuedBytes -= packetSize;
    }
    else
    {
        queuedBytes = 0;
    }

    return true;
}

bool SendQueue::TryBeginSend(std::vector<char>& outPacket)
{
    std::lock_guard<std::mutex> lock(queueMutex);

    if (isClosed || packets.empty() || !isFlushing)
    {
        return false;
    }

    outPacket = std::move(packets.front());
    packets.pop();

    // queuedBytes intentionally still includes this in-flight packet.
    return true;
}

bool SendQueue::CompleteSendAndGetNext(std::size_t completedPacketSize,
                                       std::vector<char>& outNextPacket)
{
    std::lock_guard<std::mutex> lock(queueMutex);

    if (isClosed)
    {
        return false;
    }

    if (queuedBytes < completedPacketSize)
    {
        queuedBytes = 0;
        isFlushing = false;
        return false;
    }

    queuedBytes -= completedPacketSize;

    if (packets.empty())
    {
        isFlushing = false;
        return false;
    }

    return BuildNextBatch(outNextPacket);
}

bool SendQueue::BuildNextBatch(std::vector<char>& outBatch)
{
    if (packets.empty())
    {
        return false;
    }

    const std::size_t firstPacketSize = packets.front().size();

    if (firstPacketSize >= MAX_BATCH_BYTES)
    {
        outBatch = std::move(packets.front());
        packets.pop();
        return true;
    }

    const std::size_t batchCapacity = (std::min)(queuedBytes, MAX_BATCH_BYTES);
    outBatch.clear();
    outBatch.reserve(batchCapacity);

    while (!packets.empty())
    {
        const std::vector<char>& packet = packets.front();

        if (!outBatch.empty() && outBatch.size() + packet.size() > MAX_BATCH_BYTES)
        {
            break;
        }

        outBatch.insert(outBatch.end(), packet.begin(), packet.end());
        packets.pop();
    }

    return !outBatch.empty();
}

void SendQueue::Close()
{
    std::lock_guard<std::mutex> lock(queueMutex);

    if (isClosed)
    {
        return;
    }

    isClosed = true;

    while (!packets.empty())
    {
        packets.pop();
    }

    queuedBytes = 0;
    isFlushing = false;
}
