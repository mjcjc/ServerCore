#include "IocpCore.h"

#include <utility>

IocpCore::~IocpCore()
{
    Stop();
}

bool IocpCore::Start(
    std::size_t workerCount,
    CompletionHandler completionHandler)
{
    if (workerCount == 0 ||
        !completionHandler ||
        isRunning.load())
    {
        return false;
    }

    completionPort = CreateIoCompletionPort(
        INVALID_HANDLE_VALUE,
        nullptr,
        0,
        0
    );

    if (completionPort == nullptr)
    {
        return false;
    }

    handler = std::move(completionHandler);
    isRunning = true;
    workers.reserve(workerCount);

    try
    {
        for (std::size_t index = 0; index < workerCount; ++index)
        {
            workers.emplace_back(&IocpCore::WorkerLoop, this);
        }
    }
    catch (...)
    {
        Stop();
        return false;
    }

    return true;
}

bool IocpCore::RegisterSocket(
    SOCKET socket,
    ULONG_PTR completionKey)
{
    if (!isRunning.load() || socket == INVALID_SOCKET)
    {
        return false;
    }

    HANDLE result = CreateIoCompletionPort(
        reinterpret_cast<HANDLE>(socket),
        completionPort,
        completionKey,
        0
    );

    return result == completionPort;
}

void IocpCore::Stop()
{
    if (!isRunning.exchange(false))
    {
        return;
    }

    for (std::size_t index = 0; index < workers.size(); ++index)
    {
        PostQueuedCompletionStatus(
            completionPort,
            0,
            0,
            nullptr
        );
    }

    for (std::thread& worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    workers.clear();
    handler = nullptr;

    CloseHandle(completionPort);
    completionPort = nullptr;
}

void IocpCore::WorkerLoop()
{
    while (true)
    {
        DWORD transferredBytes = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* overlapped = nullptr;

        const BOOL succeeded = GetQueuedCompletionStatus(
            completionPort,
            &transferredBytes,
            &completionKey,
            &overlapped,
            INFINITE
        );

        if (overlapped == nullptr && completionKey == 0)
        {
            if (!isRunning.load())
            {
                break;
            }

            continue;
        }

        const DWORD errorCode = succeeded
            ? ERROR_SUCCESS
            : GetLastError();

        handler(
            transferredBytes,
            completionKey,
            overlapped,
            errorCode
        );
    }
}
