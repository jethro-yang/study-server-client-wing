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

enum MessageType
{
	MSG_HEARTBEAT = 0,
	MSG_HEARTBEAT_ACK = 1,
	MSG_START = 2,
	MSG_FLOAT_DATA = 3,
	MSG_JOIN = 4,
	MSG_DISCONNECT = 5,
	MSG_INFO = 6,
	MSG_NEW_OWNER = 7
};

#pragma pack(push, 1)
struct MessageHeader
{
	int senderId;
	int msgType;
	int bodyLen;
};
#pragma pack(pop)