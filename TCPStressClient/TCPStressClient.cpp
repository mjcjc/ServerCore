#include "../Common.h"
#include "../TCPClient/PacketHeader.h"
#include "../TCPClient/PacketType.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
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
    };

    struct TestStats
    {
        std::atomic<int> connectionSucceeded = 0;
        std::atomic<int> connectionFailed = 0;
        std::atomic<int> loginSucceeded = 0;
        std::atomic<int> loginFailed = 0;
        std::atomic<long long> messagesSent = 0;
        std::atomic<long long> messagesReceived = 0;
        std::atomic<long long> sendErrors = 0;
        std::atomic<long long> receiveErrors = 0;
        std::mutex latencyMutex;
        std::vector<long long> latenciesMicroseconds;
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

    bool ConnectAndLogin(SOCKET& outSocket,
                         const TestConfig& config,
                         int clientIndex,
                         TestStats& stats)
    {
        outSocket = socket(AF_INET, SOCK_STREAM, 0);

        if (outSocket == INVALID_SOCKET)
        {
            ++stats.connectionFailed;
            return false;
        }

        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(static_cast<u_short>(config.serverPort));

        if (inet_pton(AF_INET, config.serverIp.c_str(), &serverAddress.sin_addr) != 1 ||
            connect(outSocket,
                    reinterpret_cast<sockaddr*>(&serverAddress),
                    sizeof(serverAddress)) == SOCKET_ERROR)
        {
            ++stats.connectionFailed;
            closesocket(outSocket);
            outSocket = INVALID_SOCKET;
            return false;
        }

        ++stats.connectionSucceeded;

        LoginRequest request{};
        request.header.size = static_cast<uint16_t>(sizeof(request));
        request.header.type = static_cast<uint16_t>(PacketType::LOGIN_REQUEST);
        std::snprintf(request.nickname, sizeof(request.nickname), "load_%06d", clientIndex);

        if (!SendAll(outSocket, reinterpret_cast<const char*>(&request), sizeof(request)))
        {
            ++stats.loginFailed;
            closesocket(outSocket);
            outSocket = INVALID_SOCKET;
            return false;
        }

        LoginResponse response{};

        if (!RecvAll(outSocket, reinterpret_cast<char*>(&response), sizeof(response)) ||
            response.header.size != sizeof(response) ||
            response.header.type != static_cast<uint16_t>(PacketType::LOGIN_RESPONSE) ||
            response.success == 0)
        {
            ++stats.loginFailed;
            closesocket(outSocket);
            outSocket = INVALID_SOCKET;
            return false;
        }

        ++stats.loginSucceeded;
        return true;
    }

    void ProcessReceivedPackets(std::vector<char>& receiveBuffer,
                                std::vector<long long>& localLatencies,
                                TestStats& stats)
    {
        while (receiveBuffer.size() >= sizeof(PacketHeader))
        {
            PacketHeader header{};
            std::memcpy(&header, receiveBuffer.data(), sizeof(header));

            if (header.size < sizeof(PacketHeader) || header.size > BUFSIZE)
            {
                ++stats.receiveErrors;
                receiveBuffer.clear();
                return;
            }

            if (receiveBuffer.size() < header.size)
            {
                return;
            }

            if (header.type == static_cast<uint16_t>(PacketType::CHAT_RESPONSE) &&
                header.size == sizeof(ChatResponse))
            {
                const auto* response = reinterpret_cast<const ChatResponse*>(receiveBuffer.data());

                long long sentAtMicroseconds = 0;

                if (std::sscanf(response->message, "stress:%*d:%lld:%*d", &sentAtMicroseconds) == 1)
                {
                    const long long latency = NowMicroseconds() - sentAtMicroseconds;

                    if (latency >= 0)
                    {
                        localLatencies.push_back(latency);
                    }
                }

                ++stats.messagesReceived;
            }
            else
            {
                ++stats.receiveErrors;
            }

            receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + header.size);
        }
    }

    void RunVirtualClient(const TestConfig& config,
                          int clientIndex,
                          std::atomic<int>& loginAttemptsFinished,
                          std::atomic<bool>& startLoad,
                          TestStats& stats)
    {
        SOCKET socket = INVALID_SOCKET;
        const bool loginSucceeded = ConnectAndLogin(socket, config, clientIndex, stats);

        ++loginAttemptsFinished;

        if (!loginSucceeded)
        {
            return;
        }

        while (!startLoad.load())
        {
            std::this_thread::yield();
        }

        const bool isSender = clientIndex < config.senderCount;
        const auto testEnd = Clock::now() + std::chrono::seconds(config.durationSeconds);
        const auto sendInterval = std::chrono::microseconds(1000000 / config.messagesPerSecond);
        auto nextSendTime = Clock::now();
        int sequence = 0;
        std::vector<char> receiveBuffer;
        std::vector<long long> localLatencies;
        receiveBuffer.reserve(BUFSIZE * 2);

        while (Clock::now() < testEnd)
        {
            if (isSender && Clock::now() >= nextSendTime)
            {
                ChatRequest request{};
                request.header.size = static_cast<uint16_t>(sizeof(request));
                request.header.type = static_cast<uint16_t>(PacketType::CHAT_REQUEST);
                std::snprintf(request.message,
                              sizeof(request.message),
                              "stress:%d:%lld:%d",
                              clientIndex,
                              NowMicroseconds(),
                              sequence++);

                if (SendAll(socket, reinterpret_cast<const char*>(&request), sizeof(request)))
                {
                    ++stats.messagesSent;
                }
                else
                {
                    ++stats.sendErrors;
                    break;
                }

                nextSendTime += sendInterval;
            }

            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(socket, &readSet);
            timeval timeout{};
            timeout.tv_usec = 1000;

            const int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);

            if (selectResult == SOCKET_ERROR)
            {
                ++stats.receiveErrors;
                break;
            }

            if (selectResult > 0 && FD_ISSET(socket, &readSet))
            {
                char buffer[BUFSIZE]{};
                const int receivedBytes = recv(socket, buffer, sizeof(buffer), 0);

                if (receivedBytes <= 0)
                {
                    ++stats.receiveErrors;
                    break;
                }

                receiveBuffer.insert(receiveBuffer.end(), buffer, buffer + receivedBytes);
                ProcessReceivedPackets(receiveBuffer, localLatencies, stats);
            }
        }

        shutdown(socket, SD_BOTH);
        closesocket(socket);

        if (!localLatencies.empty())
        {
            std::lock_guard<std::mutex> lock(stats.latencyMutex);
            stats.latenciesMicroseconds.insert(
                stats.latenciesMicroseconds.end(), localLatencies.begin(), localLatencies.end());
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

    void PrintResults(const TestConfig& config, TestStats& stats, double elapsedSeconds)
    {
        std::sort(stats.latenciesMicroseconds.begin(), stats.latenciesMicroseconds.end());

        long long latencySum = 0;

        for (const long long latency : stats.latenciesMicroseconds)
        {
            latencySum += latency;
        }

        const double averageLatencyMilliseconds =
            stats.latenciesMicroseconds.empty()
                ? 0.0
                : static_cast<double>(latencySum) / stats.latenciesMicroseconds.size() / 1000.0;
        const std::size_t p95Index = stats.latenciesMicroseconds.empty()
                                         ? 0
                                         : (stats.latenciesMicroseconds.size() - 1) * 95 / 100;
        const std::size_t p99Index = stats.latenciesMicroseconds.empty()
                                         ? 0
                                         : (stats.latenciesMicroseconds.size() - 1) * 99 / 100;
        const double p95Milliseconds = stats.latenciesMicroseconds.empty()
                                           ? 0.0
                                           : stats.latenciesMicroseconds[p95Index] / 1000.0;
        const double p99Milliseconds = stats.latenciesMicroseconds.empty()
                                           ? 0.0
                                           : stats.latenciesMicroseconds[p99Index] / 1000.0;
        const int receiversPerMessage =
            stats.loginSucceeded.load() > 1 ? stats.loginSucceeded.load() - 1 : 0;
        const long long expectedReceived = stats.messagesSent.load() * receiversPerMessage;
        const double messagesPerSecond =
            elapsedSeconds <= 0.0 ? 0.0 : stats.messagesReceived.load() / elapsedSeconds;

        std::printf("\n=== TCP Stress Test Result ===\n");
        std::printf("Target:              %s:%d\n", config.serverIp.c_str(), config.serverPort);
        std::printf("Configured clients:  %d\n", config.clientCount);
        std::printf("Connected:           %d\n", stats.connectionSucceeded.load());
        std::printf("Connection failed:   %d\n", stats.connectionFailed.load());
        std::printf("Login succeeded:     %d\n", stats.loginSucceeded.load());
        std::printf("Login failed:        %d\n", stats.loginFailed.load());
        std::printf("Messages sent:       %lld\n", stats.messagesSent.load());
        std::printf("Messages received:   %lld\n", stats.messagesReceived.load());
        std::printf("Expected received:   %lld\n", expectedReceived);
        std::printf("Send errors:         %lld\n", stats.sendErrors.load());
        std::printf("Receive errors:      %lld\n", stats.receiveErrors.load());
        std::printf("Receive throughput:  %.2f messages/sec\n", messagesPerSecond);
        std::printf("Average latency:     %.3f ms\n", averageLatencyMilliseconds);
        std::printf("P95 latency:         %.3f ms\n", p95Milliseconds);
        std::printf("P99 latency:         %.3f ms\n", p99Milliseconds);
        std::printf("Elapsed:             %.3f sec\n", elapsedSeconds);
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

    WSADATA winsockData{};

    if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0)
    {
        std::printf("WSAStartup failed.\n");
        return 1;
    }

    TestStats stats;
    std::atomic<int> loginAttemptsFinished = 0;
    std::atomic<bool> startLoad = false;
    std::vector<std::thread> clients;
    clients.reserve(config.clientCount);

    std::printf("Connecting %d clients to %s:%d...\n",
                config.clientCount,
                config.serverIp.c_str(),
                config.serverPort);

    for (int index = 0; index < config.clientCount; ++index)
    {
        clients.emplace_back(RunVirtualClient,
                             std::cref(config),
                             index,
                             std::ref(loginAttemptsFinished),
                             std::ref(startLoad),
                             std::ref(stats));
    }

    while (loginAttemptsFinished.load() != config.clientCount)
    {
        std::this_thread::yield();
    }

    const auto testStartedAt = Clock::now();
    startLoad = true;

    for (std::thread& client : clients)
    {
        client.join();
    }

    const double elapsedSeconds =
        std::chrono::duration<double>(Clock::now() - testStartedAt).count();

    PrintResults(config, stats, elapsedSeconds);
    WSACleanup();

    const int receiversPerMessage =
        stats.loginSucceeded.load() > 1 ? stats.loginSucceeded.load() - 1 : 0;
    const long long expectedReceived = stats.messagesSent.load() * receiversPerMessage;

    return stats.connectionFailed.load() == 0 && stats.loginFailed.load() == 0 &&
                   stats.sendErrors.load() == 0 && stats.receiveErrors.load() == 0 &&
                   stats.messagesReceived.load() == expectedReceived
               ? 0
               : 1;
}
