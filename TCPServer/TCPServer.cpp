#include "..\Common.h"
#include "NetworkCore.h"

int main()
{
	
    NetworkCore server;

    server.Initialize();
    server.bindSock();
    server.listenSock();

    printf("[TCP 서버] 시작. 포트 번호=%d\n", SERVERPORT);
    
    server.Run();
    server.closeSock();
	return 0;
}

