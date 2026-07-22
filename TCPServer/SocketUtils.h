#pragma once
#include"../Common.h"

class SocketUtils
{
public:
	static bool SendAll(SOCKET sock, const char* buf, int len);
	static bool RecvAll(SOCKET sock, char* buf, int len);
};

