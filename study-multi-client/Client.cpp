#include "Client.h"

CClient::CClient()
{
}

CClient::~CClient()
{
	// 각각 스레드함수가 마저 종료될 때 까지
	// 메인 스레드가 블록상태가 된다.
	if (mRecvThread && mRecvThread->joinable())
	{
		mRecvThread->join();
	}

	if (mHbThread && mHbThread->joinable())
	{
		mHbThread->join();
	}

	closesocket(mSock);
	WSACleanup();
}

void CClient::ReceiveThread(SOCKET sock)
{
	while (true)
	{
		MessageHeader header;
		std::vector<char> bodyBuffer;
		if (!ReceiveMsg(sock, header, bodyBuffer))
		{
			std::cout << "Disconnected from server." << std::endl;
			break;
		}

		if (header.msgType == MSG_HEARTBEAT_ACK)
		{
			continue;
		}

		std::cout << "Message from " << header.senderId << " type " << header.msgType << ": ";

		if (header.bodyLen > 0)
		{
			if (header.msgType == MSG_FLOAT_DATA && header.bodyLen == sizeof(float))
			{
				float value;
				memcpy(&value, bodyBuffer.data(), sizeof(float));
				std::cout << value;
			}
			else
			{
				// body를 문자열로 간주 (널 종료 문자 포함)
				std::cout << bodyBuffer.data();
			}
		}
		std::cout << std::endl;
	}
}

void CClient::HeartbeatThread(SOCKET sock)
{
	// 1초마다 하트비트 메시지 전송
	while (true)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		SendMsg(sock, 0, MSG_HEARTBEAT, nullptr, 0);
	}
}

bool CClient::SendMsg(SOCKET sock, int senderId, int msgType, const void* body, int bodyLen)
{
	MessageHeader header;
	header.senderId = senderId;
	header.msgType = msgType;
	header.bodyLen = bodyLen;

	if (!SendAll(sock, reinterpret_cast<char*>(&header), sizeof(header)))
		return false;

	if (bodyLen > 0 && body != nullptr)
	{
		if (!SendAll(sock, reinterpret_cast<const char*>(body), bodyLen))
			return false;
	}
	return true;
}

bool CClient::SendAll(SOCKET sock, const char* data, int length)
{
	int totalSent = 0;
	while (totalSent < length)
	{
		int sent = send(sock, data + totalSent, length - totalSent, 0);
		if (sent == SOCKET_ERROR)
			return false;

		totalSent += sent;
	}
	return true;
}

bool CClient::ReceiveMsg(SOCKET sock, MessageHeader& header, std::vector<char>& bodyBuffer)
{
	if (!RecvAll(sock, reinterpret_cast<char*>(&header), sizeof(header)))
		return false;

	bodyBuffer.resize(header.bodyLen);
	if (header.bodyLen > 0)
	{
		if (!RecvAll(sock, bodyBuffer.data(), header.bodyLen))
			return false;
	}
	return true;
}

bool CClient::RecvAll(SOCKET sock, char* buffer, int length)
{
	int totalReceived = 0;

	while (totalReceived < length)
	{
		int received = recv(sock, buffer + totalReceived, length - totalReceived, 0);
		if (received <= 0)
			return false;

		totalReceived += received;
	}
	return true;
}

bool CClient::Init()
{
	if (WSAStartup(MAKEWORD(2, 2), &mWsaData) != 0)
	{
		std::cerr << "WSAStartup failed." << std::endl;
		return false;
	}

	mSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (mSock == INVALID_SOCKET)
	{
		std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
		WSACleanup();
		return false;
	}

	mServerAddr.sin_family = AF_INET;
	mServerAddr.sin_port = htons(PORT);

	if (inet_pton(AF_INET, SERVER_IP, &mServerAddr.sin_addr) <= 0)
	{
		std::cerr << "Invalid address." << std::endl;
		closesocket(mSock);
		WSACleanup();
		return false;
	}

	if (connect(mSock, (sockaddr*)&mServerAddr, sizeof(mServerAddr)) == SOCKET_ERROR)
	{
		std::cerr << "Connection failed: " << WSAGetLastError() << std::endl;
		closesocket(mSock);
		WSACleanup();
		return false;
	}

	std::cout << "Connected to server." << std::endl;
	mRecvThread = std::make_unique<std::thread>(&CClient::ReceiveThread, this, mSock);
	mHbThread = std::make_unique<std::thread>(&CClient::HeartbeatThread, this, mSock);

	return true;
}
