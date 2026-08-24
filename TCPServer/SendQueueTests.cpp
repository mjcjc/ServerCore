#include "SendQueue.h"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    constexpr std::size_t QUEUE_LIMIT = 64 * 1024;

    int failures = 0;

    void Expect(bool condition, const char* testName)
    {
        if (condition)
        {
            std::cout << "[PASS] " << testName << '\n';
            return;
        }

        std::cerr << "[FAIL] " << testName << '\n';
        ++failures;
    }

    void TestRejectsInvalidPackets()
    {
        SendQueue queue;
        const char value = 'x';

        Expect(queue.Enqueue(nullptr, 1) == EnqueueResult::REJECTED, "null packet is rejected");
        Expect(queue.Enqueue(&value, 0) == EnqueueResult::REJECTED, "empty packet is rejected");
    }

    void TestAcceptsExactLimit()
    {
        SendQueue queue;
        std::vector<char> packet(QUEUE_LIMIT, 'a');

        Expect(queue.Enqueue(packet.data(), packet.size()) == EnqueueResult::START_FLUSHING,
               "exactly 64 KB is accepted");
    }

    void TestRejectsPacketLargerThanLimit()
    {
        SendQueue queue;
        std::vector<char> packet(QUEUE_LIMIT + 1, 'a');

        Expect(queue.Enqueue(packet.data(), packet.size()) == EnqueueResult::QUEUE_FULL,
               "64 KB plus 1 byte is rejected");
    }

    void TestRejectsAccumulatedOverflow()
    {
        SendQueue queue;
        std::vector<char> firstPacket(40 * 1024, 'a');
        std::vector<char> secondPacket(24 * 1024, 'b');
        const char extraByte = 'c';

        Expect(queue.Enqueue(firstPacket.data(), firstPacket.size()) ==
                   EnqueueResult::START_FLUSHING,
               "first packet starts flushing");
        Expect(queue.Enqueue(secondPacket.data(), secondPacket.size()) == EnqueueResult::QUEUED,
               "packets may fill the queue to exactly 64 KB");
        Expect(queue.Enqueue(&extraByte, 1) == EnqueueResult::QUEUE_FULL,
               "accumulated queue size cannot exceed 64 KB");
    }

    void TestCapacityReturnsAfterPop()
    {
        SendQueue queue;
        std::vector<char> fullPacket(QUEUE_LIMIT, 'a');
        std::vector<char> poppedPacket;
        const char nextByte = 'b';

        queue.Enqueue(fullPacket.data(), fullPacket.size());

        Expect(queue.TryPop(poppedPacket) && poppedPacket.size() == QUEUE_LIMIT,
               "pop returns the queued packet");
        Expect(queue.Enqueue(&nextByte, 1) == EnqueueResult::QUEUED,
               "popping a packet restores queue capacity");
    }

    void TestCloseRejectsNewPackets()
    {
        SendQueue queue;
        const char value = 'x';

        queue.Close();

        Expect(queue.Enqueue(&value, 1) == EnqueueResult::REJECTED,
               "closed queue rejects new packets");
    }

    void TestConcurrentEnqueueKeepsEveryPacket()
    {
        constexpr int THREAD_COUNT = 100;

        SendQueue queue;
        std::atomic<int> readyThreads = 0;
        std::atomic<bool> start = false;
        std::atomic<int> startFlushingCount = 0;
        std::atomic<int> queuedCount = 0;
        std::atomic<int> unexpectedResultCount = 0;
        std::vector<int> packetIds(THREAD_COUNT);
        std::vector<std::thread> threads;

        threads.reserve(THREAD_COUNT);

        for (int index = 0; index < THREAD_COUNT; ++index)
        {
            packetIds[index] = index;

            threads.emplace_back(
                [&, index]()
                {
                    ++readyThreads;

                    while (!start.load())
                    {
                        std::this_thread::yield();
                    }

                    const EnqueueResult result = queue.Enqueue(
                        reinterpret_cast<const char*>(&packetIds[index]), sizeof(packetIds[index]));

                    if (result == EnqueueResult::START_FLUSHING)
                    {
                        ++startFlushingCount;
                    }
                    else if (result == EnqueueResult::QUEUED)
                    {
                        ++queuedCount;
                    }
                    else
                    {
                        ++unexpectedResultCount;
                    }
                });
        }

        while (readyThreads.load() != THREAD_COUNT)
        {
            std::this_thread::yield();
        }

        start = true;

        for (std::thread& thread : threads)
        {
            thread.join();
        }

        std::vector<bool> seenPacketIds(THREAD_COUNT, false);
        std::vector<char> packet;
        int poppedCount = 0;
        bool packetsAreValidAndUnique = true;

        while (queue.TryPop(packet))
        {
            if (packet.size() != sizeof(int))
            {
                packetsAreValidAndUnique = false;
                continue;
            }

            int packetId = -1;
            std::memcpy(&packetId, packet.data(), sizeof(packetId));

            if (packetId < 0 || packetId >= THREAD_COUNT || seenPacketIds[packetId])
            {
                packetsAreValidAndUnique = false;
                continue;
            }

            seenPacketIds[packetId] = true;
            ++poppedCount;
        }

        Expect(startFlushingCount.load() == 1, "only one concurrent sender starts flushing");
        Expect(queuedCount.load() == THREAD_COUNT - 1 && unexpectedResultCount.load() == 0,
               "all other concurrent packets are queued");
        Expect(poppedCount == THREAD_COUNT && packetsAreValidAndUnique,
               "concurrent enqueue loses or duplicates no packets");
    }

    void TestConcurrentEnqueueRespectsCapacity()
    {
        constexpr int THREAD_COUNT = 100;
        constexpr std::size_t PACKET_SIZE = 1024;
        constexpr int EXPECTED_ACCEPTED_COUNT = static_cast<int>(QUEUE_LIMIT / PACKET_SIZE);
        constexpr int EXPECTED_REJECTED_COUNT = THREAD_COUNT - EXPECTED_ACCEPTED_COUNT;

        SendQueue queue;
        std::atomic<int> readyThreads = 0;
        std::atomic<bool> start = false;
        std::atomic<int> startFlushingCount = 0;
        std::atomic<int> queuedCount = 0;
        std::atomic<int> queueFullCount = 0;
        std::atomic<int> unexpectedResultCount = 0;
        std::vector<std::vector<char>> packets(THREAD_COUNT, std::vector<char>(PACKET_SIZE));
        std::vector<std::thread> threads;

        threads.reserve(THREAD_COUNT);

        for (int index = 0; index < THREAD_COUNT; ++index)
        {
            packets[index][0] = static_cast<char>(index);

            threads.emplace_back(
                [&, index]()
                {
                    ++readyThreads;

                    while (!start.load())
                    {
                        std::this_thread::yield();
                    }

                    const EnqueueResult result =
                        queue.Enqueue(packets[index].data(), packets[index].size());

                    switch (result)
                    {
                    case EnqueueResult::START_FLUSHING:
                        ++startFlushingCount;
                        break;
                    case EnqueueResult::QUEUED:
                        ++queuedCount;
                        break;
                    case EnqueueResult::QUEUE_FULL:
                        ++queueFullCount;
                        break;
                    default:
                        ++unexpectedResultCount;
                        break;
                    }
                });
        }

        while (readyThreads.load() != THREAD_COUNT)
        {
            std::this_thread::yield();
        }

        start = true;

        for (std::thread& thread : threads)
        {
            thread.join();
        }

        std::vector<char> poppedPacket;
        int poppedCount = 0;
        std::size_t poppedBytes = 0;
        bool everyPacketHasExpectedSize = true;

        while (queue.TryPop(poppedPacket))
        {
            everyPacketHasExpectedSize =
                everyPacketHasExpectedSize && poppedPacket.size() == PACKET_SIZE;
            ++poppedCount;
            poppedBytes += poppedPacket.size();
        }

        Expect(startFlushingCount.load() == 1 && queuedCount.load() == EXPECTED_ACCEPTED_COUNT - 1,
               "concurrent queue accepts exactly 64 one-KB packets");
        Expect(queueFullCount.load() == EXPECTED_REJECTED_COUNT &&
                   unexpectedResultCount.load() == 0,
               "concurrent queue rejects all packets beyond 64 KB");
        Expect(poppedCount == EXPECTED_ACCEPTED_COUNT && poppedBytes == QUEUE_LIMIT &&
                   everyPacketHasExpectedSize,
               "concurrent queue never stores more than 64 KB");
    }

    void TestConcurrentEnqueueAndPop()
    {
        constexpr int THREAD_COUNT = 100;

        SendQueue queue;
        std::atomic<int> readyThreads = 0;
        std::atomic<bool> start = false;
        std::atomic<int> startFlushingCount = 0;
        std::atomic<int> unexpectedResultCount = 0;
        std::mutex poppedIdsMutex;
        std::vector<int> packetIds(THREAD_COUNT);
        std::vector<int> poppedIds;
        std::vector<std::thread> threads;

        poppedIds.reserve(THREAD_COUNT);
        threads.reserve(THREAD_COUNT);

        for (int index = 0; index < THREAD_COUNT; ++index)
        {
            packetIds[index] = index;

            threads.emplace_back(
                [&, index]()
                {
                    ++readyThreads;

                    while (!start.load())
                    {
                        std::this_thread::yield();
                    }

                    const EnqueueResult result = queue.Enqueue(
                        reinterpret_cast<const char*>(&packetIds[index]), sizeof(packetIds[index]));

                    if (result == EnqueueResult::QUEUED)
                    {
                        return;
                    }

                    if (result != EnqueueResult::START_FLUSHING)
                    {
                        ++unexpectedResultCount;
                        return;
                    }

                    ++startFlushingCount;

                    std::vector<char> poppedPacket;

                    while (queue.TryPop(poppedPacket))
                    {
                        if (poppedPacket.size() != sizeof(int))
                        {
                            ++unexpectedResultCount;
                            continue;
                        }

                        int poppedId = -1;
                        std::memcpy(&poppedId, poppedPacket.data(), sizeof(poppedId));

                        {
                            std::lock_guard<std::mutex> lock(poppedIdsMutex);
                            poppedIds.push_back(poppedId);
                        }

                        std::this_thread::yield();
                    }
                });
        }

        while (readyThreads.load() != THREAD_COUNT)
        {
            std::this_thread::yield();
        }

        start = true;

        for (std::thread& thread : threads)
        {
            thread.join();
        }

        std::vector<bool> seenPacketIds(THREAD_COUNT, false);
        bool packetsAreValidAndUnique = true;

        for (const int packetId : poppedIds)
        {
            if (packetId < 0 || packetId >= THREAD_COUNT || seenPacketIds[packetId])
            {
                packetsAreValidAndUnique = false;
                continue;
            }

            seenPacketIds[packetId] = true;
        }

        Expect(startFlushingCount.load() >= 1, "concurrent enqueue starts a queue flusher");
        Expect(poppedIds.size() == THREAD_COUNT && packetsAreValidAndUnique &&
                   unexpectedResultCount.load() == 0,
               "concurrent enqueue and pop lose or duplicate no packets");
    }

    void TestConcurrentClose()
    {
        constexpr int ENQUEUE_THREAD_COUNT = 100;

        SendQueue queue;
        std::atomic<int> readyThreads = 0;
        std::atomic<bool> start = false;
        std::atomic<bool> closeFinished = false;
        std::atomic<int> unexpectedResultCount = 0;
        std::vector<int> packetIds(ENQUEUE_THREAD_COUNT);
        std::vector<std::thread> threads;

        threads.reserve(ENQUEUE_THREAD_COUNT + 2);

        for (int index = 0; index < ENQUEUE_THREAD_COUNT; ++index)
        {
            packetIds[index] = index;

            threads.emplace_back(
                [&, index]()
                {
                    ++readyThreads;

                    while (!start.load())
                    {
                        std::this_thread::yield();
                    }

                    const EnqueueResult result = queue.Enqueue(
                        reinterpret_cast<const char*>(&packetIds[index]), sizeof(packetIds[index]));

                    if (result != EnqueueResult::START_FLUSHING &&
                        result != EnqueueResult::QUEUED && result != EnqueueResult::REJECTED)
                    {
                        ++unexpectedResultCount;
                    }
                });
        }

        threads.emplace_back(
            [&]()
            {
                ++readyThreads;

                while (!start.load())
                {
                    std::this_thread::yield();
                }

                std::vector<char> poppedPacket;

                while (!closeFinished.load())
                {
                    queue.TryPop(poppedPacket);
                    std::this_thread::yield();
                }
            });

        threads.emplace_back(
            [&]()
            {
                ++readyThreads;

                while (!start.load())
                {
                    std::this_thread::yield();
                }

                queue.Close();
                queue.Close();
                closeFinished = true;
            });

        while (readyThreads.load() != ENQUEUE_THREAD_COUNT + 2)
        {
            std::this_thread::yield();
        }

        start = true;

        for (std::thread& thread : threads)
        {
            thread.join();
        }

        const char packet = 'x';
        std::vector<char> poppedPacket;

        Expect(closeFinished.load() && unexpectedResultCount.load() == 0,
               "close safely races with enqueue and pop");
        Expect(queue.Enqueue(&packet, 1) == EnqueueResult::REJECTED && !queue.TryPop(poppedPacket),
               "closed queue stays empty and rejects new packets");
    }

    void TestAsyncSendKeepsInFlightBytesAndAdvancesQueue()
    {
        SendQueue queue;
        std::vector<char> firstPacket(40 * 1024, 'a');
        std::vector<char> secondPacket(24 * 1024, 'b');
        std::vector<char> inFlightPacket;
        std::vector<char> nextPacket;
        const char extraByte = 'c';

        const EnqueueResult firstResult = queue.Enqueue(firstPacket.data(), firstPacket.size());
        const bool beganFirstSend = queue.TryBeginSend(inFlightPacket);
        const EnqueueResult secondResult = queue.Enqueue(secondPacket.data(), secondPacket.size());

        Expect(firstResult == EnqueueResult::START_FLUSHING && beganFirstSend &&
                   inFlightPacket.size() == firstPacket.size() &&
                   secondResult == EnqueueResult::QUEUED,
               "async send begins one packet and queues the next");
        Expect(queue.Enqueue(&extraByte, 1) == EnqueueResult::QUEUE_FULL,
               "in-flight async packet remains inside the 64 KB limit");

        const bool hasNextPacket = queue.CompleteSendAndGetNext(inFlightPacket.size(), nextPacket);

        Expect(hasNextPacket && nextPacket.size() == secondPacket.size(),
               "send completion advances to the next queued packet");
        Expect(!queue.CompleteSendAndGetNext(nextPacket.size(), inFlightPacket) &&
                   queue.Enqueue(&extraByte, 1) == EnqueueResult::START_FLUSHING,
               "finishing the last async send releases queue capacity");
    }

    void TestAsyncSendBatchesQueuedPackets()
    {
        SendQueue queue;
        std::vector<char> firstPacket(128, 'a');
        std::vector<char> secondPacket(128, 'b');
        std::vector<char> thirdPacket(128, 'c');
        std::vector<char> inFlightPacket;
        std::vector<char> nextBatch;

        queue.Enqueue(firstPacket.data(), firstPacket.size());
        queue.TryBeginSend(inFlightPacket);
        queue.Enqueue(secondPacket.data(), secondPacket.size());
        queue.Enqueue(thirdPacket.data(), thirdPacket.size());

        const bool hasNextBatch =
            queue.CompleteSendAndGetNext(inFlightPacket.size(), nextBatch);

        Expect(hasNextBatch && nextBatch.size() == secondPacket.size() + thirdPacket.size(),
               "send completion batches queued packets into one async send");
        Expect(nextBatch[0] == 'b' && nextBatch[secondPacket.size()] == 'c',
               "batched send preserves packet order and contents");
        Expect(!queue.CompleteSendAndGetNext(nextBatch.size(), inFlightPacket),
               "batched send completion drains every included packet");
    }
}

int main()
{
    TestRejectsInvalidPackets();
    TestAcceptsExactLimit();
    TestRejectsPacketLargerThanLimit();
    TestRejectsAccumulatedOverflow();
    TestCapacityReturnsAfterPop();
    TestCloseRejectsNewPackets();
    TestConcurrentEnqueueKeepsEveryPacket();
    TestConcurrentEnqueueRespectsCapacity();
    TestConcurrentEnqueueAndPop();
    TestConcurrentClose();
    TestAsyncSendKeepsInFlightBytesAndAdvancesQueue();
    TestAsyncSendBatchesQueuedPackets();

    if (failures != 0)
    {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All SendQueue tests passed.\n";
    return 0;
}
