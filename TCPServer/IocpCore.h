#pragma once

#include "../Common.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

class IocpCore
{
  public:
    using CompletionHandler = std::function<void(
        DWORD transferredBytes, ULONG_PTR completionKey, OVERLAPPED* overlapped, DWORD errorCode)>;

    IocpCore() = default;
    ~IocpCore();

    IocpCore(const IocpCore&) = delete;
    IocpCore& operator=(const IocpCore&) = delete;

    bool Start(std::size_t workerCount, CompletionHandler completionHandler);

    // Registers a socket with the existing completion port.
    // This does not create another IOCP object.
    bool RegisterSocket(SOCKET socket, ULONG_PTR completionKey);

    void Stop();

  private:
    void WorkerLoop();

    HANDLE completionPort = nullptr;
    std::atomic<bool> isRunning = false;
    CompletionHandler handler;
    std::vector<std::thread> workers;
};
