#include "../Common.h"
#include "../TCPClient/PacketHeader.h"
#include "../TCPClient/PacketType.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    struct TestConfig
    {
        std::string serverIp = "127.0.0.1";
        int serverPort = SERVERPORT;
        int clientCount = 300;
        int senderCount = 10;
        int messagesPerSecond = 10;
        int durationSeconds = 30;
        int connectorCount = 8;
        int iocpWorkerCount = 4;
        int drainSeconds = 5;
    };

    struct TestStats
    {
        std::atomic<int> connectionSucceeded{0};
        std::atomic<int> connectionFailed{0};
        std::atomic<int> loginSucceeded{0};
        std::atomic<int> loginFailed{0};
        std::atomic<long long> messagesSent{0};
        std::atomic<long long> messagesReceived{0};
        std::atomic<long long> sendErrors{0};
        std::atomic<long long> receiveErrors{0};
    };

    struct ClientContext
    {
        SOCKET socket = INVALID_SOCKET;
        int clientIndex = 0;
        OVERLAPPED receiveOverlapped{};
        WSABUF receiveWsaBuffer{};
        std::array<char, BUFSIZE> receiveIoBuffer{};
        std::vector<char> pendingBytes;
        std::vector<long long> latenciesMicroseconds;
        std::atomic<bool> connected{true};
    };

    bool SendAll(SOCKET socket, const char* data, int size)
    {
        int sentBytes = 0;

        while (sentBytes < size)
        {
            const int result = send(socket, data + sentBytes, size - sentBytes, 0);

            if (result == SOCKET_ERROR || result == 0)
            {
                return false;
            }

            sentBytes += result;
        }

        return true;
    }

    bool RecvAll(SOCKET socket, char* data, int size)
    {
        int receivedBytes = 0;

        while (receivedBytes < size)
        {
            const int result = recv(socket, data + receivedBytes, size - receivedBytes, 0);

            if (result == SOCKET_ERROR || result == 0)
            {
                return false;
            }

            receivedBytes += result;
        }

        return true;
    }

    long long NowMicroseconds()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   Clock::now().time_since_epoch())
            .count();
    }

    std::unique_ptr<ClientContext> ConnectAndLogin(const TestConfig& config,
                                                   int clientIndex,
                                                   TestStats& stats)
    {
        auto context = std::make_unique<ClientContext>();
        context->clientIndex = clientIndex;
        context->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (context->socket == INVALID_SOCKET)
        {
            ++stats.connectionFailed;
            return nullptr;
        }

        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(static_cast<u_short>(config.serverPort));

        if (inet_pton(AF_INET, config.serverIp.c_str(), &serverAddress.sin_addr) != 1 ||
            connect(context->socket,
                    reinterpret_cast<sockaddr*>(&serverAddress),
                    sizeof(serverAddress)) == SOCKET_ERROR)
        {
            ++stats.connectionFailed;
            closesocket(context->socket);
            return nullptr;
        }

        ++stats.connectionSucceeded;

        LoginRequest request{};
        request.header.size = static_cast<std::uint16_t>(sizeof(request));
        request.header.type = static_cast<std::uint16_t>(PacketType::LOGIN_REQUEST);
        std::snprintf(request.nickname,
                      sizeof(request.nickname),
                      "p%d_%06d",
                      config.serverPort,
                      clientIndex);

        LoginResponse response{};

        if (!SendAll(context->socket, reinterpret_cast<const char*>(&request), sizeof(request)) ||
            !RecvAll(context->socket, reinterpret_cast<char*>(&response), sizeof(response)) ||
            response.header.size != sizeof(response) ||
            response.header.type != static_cast<std::uint16_t>(PacketType::LOGIN_RESPONSE) ||
            response.success == 0)
        {
            ++stats.loginFailed;
            closesocket(context->socket);
            return nullptr;
        }

        ++stats.loginSucceeded;
        context->pendingBytes.reserve(BUFSIZE * 2);
        return context;
    }

    bool PostReceive(ClientContext& context)
    {
        std::memset(&context.receiveOverlapped, 0, sizeof(context.receiveOverlapped));
        context.receiveWsaBuffer.buf = context.receiveIoBuffer.data();
        context.receiveWsaBuffer.len = static_cast<ULONG>(context.receiveIoBuffer.size());
        DWORD flags = 0;
        DWORD receivedBytes = 0;
        const int result = WSARecv(context.socket,
                                   &context.receiveWsaBuffer,
                                   1,
                                   &receivedBytes,
                                   &flags,
                                   &context.receiveOverlapped,
                                   nullptr);

        return result == 0 || WSAGetLastError() == WSA_IO_PENDING;
    }

    void ProcessReceivedPackets(ClientContext& context, TestStats& stats)
    {
        std::size_t consumedBytes = 0;

        while (context.pendingBytes.size() - consumedBytes >= sizeof(PacketHeader))
        {
            PacketHeader header{};
            std::memcpy(&header, context.pendingBytes.data() + consumedBytes, sizeof(header));

            if (header.size < sizeof(PacketHeader) || header.size > BUFSIZE)
            {
                ++stats.receiveErrors;
                context.pendingBytes.clear();
                return;
            }

            if (context.pendingBytes.size() - consumedBytes < header.size)
            {
                break;
            }

            const char* packet = context.pendingBytes.data() + consumedBytes;

            if (header.type == static_cast<std::uint16_t>(PacketType::CHAT_RESPONSE) &&
                header.size == sizeof(ChatResponse))
            {
                const ChatResponse* response = reinterpret_cast<const ChatResponse*>(packet);
                long long sentAtMicroseconds = 0;

                if (std::sscanf(response->message,
                                "stress:%*d:%lld:%*d",
                                &sentAtMicroseconds) == 1)
                {
                    const long long latency = NowMicroseconds() - sentAtMicroseconds;

                    if (latency >= 0)
                    {
                        context.latenciesMicroseconds.push_back(latency);
                    }
                }

                ++stats.messagesReceived;
            }
            else
            {
                ++stats.receiveErrors;
            }

            consumedBytes += header.size;
        }

        if (consumedBytes > 0)
        {
            context.pendingBytes.erase(context.pendingBytes.begin(),
                                       context.pendingBytes.begin() + consumedBytes);
        }
    }

    void RunIocpWorker(HANDLE completionPort,
                       std::atomic<bool>& receiving,
                       TestStats& stats)
    {
        while (true)
        {
            DWORD transferredBytes = 0;
            ULONG_PTR completionKey = 0;
            OVERLAPPED* overlapped = nullptr;
            const BOOL succeeded = GetQueuedCompletionStatus(completionPort,
                                                              &transferredBytes,
                                                              &completionKey,
                                                              &overlapped,
                                                              INFINITE);

            if (completionKey == 0 && overlapped == nullptr)
            {
                return;
            }

            ClientContext* context = reinterpret_cast<ClientContext*>(completionKey);

            if (context == nullptr)
            {
                continue;
            }

            if (!succeeded || transferredBytes == 0)
            {
                if (receiving.load() && context->connected.exchange(false))
                {
                    ++stats.receiveErrors;
                }

                continue;
            }

            context->pendingBytes.insert(context->pendingBytes.end(),
                                         context->receiveIoBuffer.begin(),
                                         context->receiveIoBuffer.begin() + transferredBytes);
            ProcessReceivedPackets(*context, stats);

            if (receiving.load() && context->connected.load() && !PostReceive(*context))
            {
                context->connected.store(false);
                ++stats.receiveErrors;
            }
        }
    }

    bool ParsePositiveInt(const char* text, int& outValue)
    {
        char* end = nullptr;
        const long value = std::strtol(text, &end, 10);

        if (end == text || *end != '\0' || value <= 0 || value > 100000)
        {
            return false;
        }

        outValue = static_cast<int>(value);
        return true;
    }

    double PercentileMilliseconds(const std::vector<long long>& latencies, int percentile)
    {
        if (latencies.empty())
        {
            return 0.0;
        }

        const std::size_t index = (latencies.size() - 1) * percentile / 100;
        return latencies[index] / 1000.0;
    }

    void PrintResults(const TestConfig& config,
                      const TestStats& stats,
                      int activeClientCount,
                      std::vector<long long>& latencies,
                      double elapsedSeconds)
    {
        std::sort(latencies.begin(), latencies.end());
        long long latencySum = 0;

        for (const long long latency : latencies)
        {
            latencySum += latency;
        }

        const double averageLatencyMilliseconds =
            latencies.empty()
                ? 0.0
                : static_cast<double>(latencySum) / latencies.size() / 1000.0;
        const int receiversPerMessage = activeClientCount > 1 ? activeClientCount - 1 : 0;
        const long long expectedReceived = stats.messagesSent.load() * receiversPerMessage;
        const double messagesPerSecond =
            elapsedSeconds <= 0.0 ? 0.0 : stats.messagesReceived.load() / elapsedSeconds;

        std::printf("\n=== IOCP TCP Stress Test Result ===\n");
        std::printf("Target:              %s:%d\n", config.serverIp.c_str(), config.serverPort);
        std::printf("Configured clients:  %d\n", config.clientCount);
        std::printf("Connector threads:   %d\n", config.connectorCount);
        std::printf("IOCP worker threads: %d\n", config.iocpWorkerCount);
        std::printf("Connected:           %d\n", stats.connectionSucceeded.load());
        std::printf("Connection failed:   %d\n", stats.connectionFailed.load());
        std::printf("Login succeeded:     %d\n", stats.loginSucceeded.load());
        std::printf("Login failed:        %d\n", stats.loginFailed.load());
        std::printf("Active clients:      %d\n", activeClientCount);
        std::printf("Messages sent:       %lld\n", stats.messagesSent.load());
        std::printf("Messages received:   %lld\n", stats.messagesReceived.load());
        std::printf("Expected received:   %lld\n", expectedReceived);
        std::printf("Send errors:         %lld\n", stats.sendErrors.load());
        std::printf("Receive errors:      %lld\n", stats.receiveErrors.load());
        std::printf("Receive throughput:  %.2f messages/sec\n", messagesPerSecond);
        std::printf("Average latency:     %.3f ms\n", averageLatencyMilliseconds);
        std::printf("P95 latency:         %.3f ms\n", PercentileMilliseconds(latencies, 95));
        std::printf("P99 latency:         %.3f ms\n", PercentileMilliseconds(latencies, 99));
        std::printf("Load duration:       %.3f sec\n", elapsedSeconds);
    }
}

int main(int argc, char* argv[])
{
    TestConfig config;

    if (argc > 1)
    {
        config.serverIp = argv[1];
    }

    if ((argc > 2 && !ParsePositiveInt(argv[2], config.serverPort)) || config.serverPort > 65535 ||
        (argc > 3 && !ParsePositiveInt(argv[3], config.clientCount)) ||
        (argc > 4 && !ParsePositiveInt(argv[4], config.senderCount)) ||
        (argc > 5 && !ParsePositiveInt(argv[5], config.messagesPerSecond)) ||
        (argc > 6 && !ParsePositiveInt(argv[6], config.durationSeconds)) ||
        config.senderCount > config.clientCount)
    {
        std::printf("Usage: TCPStressClient.exe [serverIp] [port] [clients] "
                    "[senders] [messagesPerSecond] [durationSeconds]\n");
        return 1;
    }

    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    config.iocpWorkerCount = std::clamp(static_cast<int>(hardwareThreads), 2, 8);
    config.connectorCount = (std::min)(config.connectorCount, config.clientCount);

    WSADATA winsockData{};

    if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0)
    {
        std::printf("WSAStartup failed.\n");
        return 1;
    }

    TestStats stats;
    std::vector<std::unique_ptr<ClientContext>> clients(config.clientCount);
    std::atomic<int> nextClientIndex{0};
    std::vector<std::thread> connectorThreads;
    connectorThreads.reserve(config.connectorCount);

    std::printf("Connecting %d clients to %s:%d with %d connector threads...\n",
                config.clientCount,
                config.serverIp.c_str(),
                config.serverPort,
                config.connectorCount);

    for (int index = 0; index < config.connectorCount; ++index)
    {
        connectorThreads.emplace_back([&config, &stats, &clients, &nextClientIndex]()
        {
            while (true)
            {
                const int clientIndex = nextClientIndex.fetch_add(1);

                if (clientIndex >= config.clientCount)
                {
                    return;
                }

                clients[clientIndex] = ConnectAndLogin(config, clientIndex, stats);
            }
        });
    }

    for (std::thread& connector : connectorThreads)
    {
        connector.join();
    }

    const HANDLE completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

    if (completionPort == nullptr)
    {
        std::printf("CreateIoCompletionPort failed. Error=%lu\n", GetLastError());
        WSACleanup();
        return 1;
    }

    std::vector<ClientContext*> activeClients;
    activeClients.reserve(stats.loginSucceeded.load());

    for (const auto& context : clients)
    {
        if (!context)
        {
            continue;
        }

        if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(context->socket),
                                   completionPort,
                                   reinterpret_cast<ULONG_PTR>(context.get()),
                                   0) == nullptr ||
            !PostReceive(*context))
        {
            context->connected.store(false);
            closesocket(context->socket);
            ++stats.receiveErrors;
            continue;
        }

        activeClients.push_back(context.get());
    }

    std::atomic<bool> receiving{true};
    std::vector<std::thread> iocpWorkers;
    iocpWorkers.reserve(config.iocpWorkerCount);

    for (int index = 0; index < config.iocpWorkerCount; ++index)
    {
        iocpWorkers.emplace_back(RunIocpWorker,
                                 completionPort,
                                 std::ref(receiving),
                                 std::ref(stats));
    }

    const int activeSenderCount = (std::min)(config.senderCount,
                                             static_cast<int>(activeClients.size()));
    const auto sendInterval = std::chrono::microseconds(1000000 / config.messagesPerSecond);
    const auto loadStartedAt = Clock::now();
    const auto loadEndsAt = loadStartedAt + std::chrono::seconds(config.durationSeconds);
    auto nextSendAt = loadStartedAt;
    int sequence = 0;

    while (Clock::now() < loadEndsAt)
    {
        const auto now = Clock::now();

        if (now < nextSendAt)
        {
            std::this_thread::sleep_until(nextSendAt);
            continue;
        }

        for (int senderIndex = 0; senderIndex < activeSenderCount; ++senderIndex)
        {
            ClientContext* sender = activeClients[senderIndex];

            if (!sender->connected.load())
            {
                continue;
            }

            ChatRequest request{};
            request.header.size = static_cast<std::uint16_t>(sizeof(request));
            request.header.type = static_cast<std::uint16_t>(PacketType::CHAT_REQUEST);
            std::snprintf(request.message,
                          sizeof(request.message),
                          "stress:%d:%lld:%d",
                          sender->clientIndex,
                          NowMicroseconds(),
                          sequence++);

            if (SendAll(sender->socket, reinterpret_cast<const char*>(&request), sizeof(request)))
            {
                ++stats.messagesSent;
            }
            else
            {
                sender->connected.store(false);
                ++stats.sendErrors;
            }
        }

        nextSendAt += sendInterval;
    }

    const double loadSeconds =
        std::chrono::duration<double>(Clock::now() - loadStartedAt).count();
    const long long expectedReceived =
        stats.messagesSent.load() * (std::max)(0, static_cast<int>(activeClients.size()) - 1);
    const auto drainDeadline = Clock::now() + std::chrono::seconds(config.drainSeconds);

    while (stats.messagesReceived.load() < expectedReceived && Clock::now() < drainDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    receiving.store(false);

    for (ClientContext* context : activeClients)
    {
        context->connected.store(false);
        shutdown(context->socket, SD_BOTH);
        closesocket(context->socket);
        context->socket = INVALID_SOCKET;
    }

    for (int index = 0; index < config.iocpWorkerCount; ++index)
    {
        PostQueuedCompletionStatus(completionPort, 0, 0, nullptr);
    }

    for (std::thread& worker : iocpWorkers)
    {
        worker.join();
    }

    CloseHandle(completionPort);
    std::vector<long long> latencies;
    latencies.reserve(static_cast<std::size_t>(stats.messagesReceived.load()));

    for (const auto& context : clients)
    {
        if (context)
        {
            latencies.insert(latencies.end(),
                             context->latenciesMicroseconds.begin(),
                             context->latenciesMicroseconds.end());
        }
    }

    PrintResults(config,
                 stats,
                 static_cast<int>(activeClients.size()),
                 latencies,
                 loadSeconds);
    WSACleanup();

    return stats.connectionSucceeded.load() == config.clientCount &&
                   stats.connectionFailed.load() == 0 &&
                   stats.loginSucceeded.load() == config.clientCount &&
                   stats.loginFailed.load() == 0 &&
                   stats.sendErrors.load() == 0 &&
                   stats.receiveErrors.load() == 0 &&
                   stats.messagesReceived.load() == expectedReceived
               ? 0
               : 1;
}
