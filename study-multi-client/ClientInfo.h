#pragma once

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#pragma comment(lib, "ws2_32.lib")

#define PORT 12345
#define SERVER_IP "127.0.0.1"

namespace ClientMessage
{
	enum class Type
	{
		MSG_HEARTBEAT,
		MSG_START,
		MSG_FLOAT_DATA,
	};
}

namespace ServerMessage
{
	enum class Type
	{
		MSG_CONNECTED,
		MSG_HEARTBEAT_ACK,
		MSG_START_ACK,
		MSG_FLOAT_DATA_ACK,
		MSG_JOIN,
		MSG_DISCONNECT,
		MSG_INFO,
		MSG_NEW_OWNER,
	};
}

#pragma pack(push, 1)
struct MessageHeader
{
	int senderId;
	int msgType;
	int bodyLen;
};
#pragma pack(pop)