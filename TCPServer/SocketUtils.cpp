#include "SocketUtils.h"

bool SocketUtils::RecvAll(SOCKET sock, char* data, int len)
{
	int total = 0;

	while (total < len)
	{
		int ret = recv(sock, data + total, len - total, 0);

		if (ret == SOCKET_ERROR || ret == 0)
			return false;

		total += ret;
	}

	return true;
}
