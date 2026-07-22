#pragma once
#include"../Common.h"

enum class PacketType : uint16_t
{
	LOGIN = 1,
	LOGOUT = 2,
	MESSAGE = 3,
};