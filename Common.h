#pragma once
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>

#include <tchar.h>  // _T(), ...
#include <stdio.h>  // printf(), ...
#include <stdlib.h> // exit(), ...
#include <string.h> // strncpy(), ...
#include <thread>

#pragma comment(lib, "ws2_32")

#define SERVERPORT 9000
#define BUFSIZE 512
inline void err_quit(const char* msg)
{
    LPVOID lpMsgBuf;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                   NULL,
                   WSAGetLastError(),
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (char*)&lpMsgBuf,
                   0,
                   NULL);
    MessageBoxA(NULL, (const char*)lpMsgBuf, msg, MB_ICONERROR);
    LocalFree(lpMsgBuf);
    exit(1);
}

inline void err_display(const char* msg)
{
    LPVOID lpMsgBuf;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                   NULL,
                   WSAGetLastError(),
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (char*)&lpMsgBuf,
                   0,
                   NULL);
    printf("[%s] %s\n", msg, (char*)lpMsgBuf);
    LocalFree(lpMsgBuf);
}

inline void err_display(int errcode)
{
    LPVOID lpMsgBuf;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                   NULL,
                   errcode,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (char*)&lpMsgBuf,
                   0,
                   NULL);
    printf("[Error] %s\n", (char*)lpMsgBuf);
    LocalFree(lpMsgBuf);
}
