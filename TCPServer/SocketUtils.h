#pragma once
#include "../Common.h"

class SocketUtils
{
  public:
    static bool RecvAll(SOCKET sock, char* buf, int len);
};
