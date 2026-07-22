#pragma once
#include"NetworkCore.h"
#include"../Common.h"

class NetworkCore;

class Session
{

public:
    Session(SOCKET client_sock, sockaddr_in clientaddr, NetworkCore* server);

    void Process();
private:
    SOCKET client_sock;
    sockaddr_in clientaddr;
    char buf[BUFSIZE + 1];
    NetworkCore* server;
};

