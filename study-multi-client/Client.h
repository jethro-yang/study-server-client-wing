#pragma once

#include "ClientInfo.h"

class CClient
{
private:
	WSADATA mWsaData;
	SOCKET mSock;
	sockaddr_in mServerAddr;

	std::unique_ptr<std::thread> mRecvThread;
	std::unique_ptr<std::thread> mHbThread;

public:
	CClient();
	~CClient();

	void SendMsg(int senderId, int msgType, const void* body, int bodyLen)
	{
		SendMsg(mSock, senderId, msgType, body, bodyLen);
	}

private:
	void ReceiveThread(SOCKET sock);
	void HeartbeatThread(SOCKET sock);

	bool SendMsg(SOCKET sock, int senderId, int msgType, const void* body, int bodyLen);
	bool SendAll(SOCKET sock, const char* data, int length);

	bool ReceiveMsg(SOCKET sock, MessageHeader& header, std::vector<char>& bodyBuffer);
	bool RecvAll(SOCKET sock, char* buffer, int length);

public:
	bool Init();

};

