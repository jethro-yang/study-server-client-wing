#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdlib>
#include <cstring>
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

struct RecvMessage
{
	int senderId;
	int msgType;
	std::vector<char> body;
};

class CClient
{
private:
	WSADATA mWsaData;
	SOCKET mSock;
	sockaddr_in mServerAddr;

	std::unique_ptr<std::thread> mRecvThread;
	std::unique_ptr<std::thread> mHbThread;

	std::mutex mQueueMutex;
	std::queue<RecvMessage> mMessageQueue;

public:
	CClient() {}
	~CClient()
	{
		if (mRecvThread && mRecvThread->joinable()) mRecvThread->join();
		if (mHbThread && mHbThread->joinable()) mHbThread->join();
		closesocket(mSock);
		WSACleanup();
	}

	bool Init()
	{
		if (WSAStartup(MAKEWORD(2, 2), &mWsaData) != 0)
		{
			std::cerr << "WSAStartup failed.\n";
			return false;
		}

		mSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (mSock == INVALID_SOCKET)
		{
			std::cerr << "Socket creation failed: " << WSAGetLastError() << "\n";
			WSACleanup();
			return false;
		}

		mServerAddr.sin_family = AF_INET;
		mServerAddr.sin_port = htons(PORT);
		if (inet_pton(AF_INET, SERVER_IP, &mServerAddr.sin_addr) <= 0)
		{
			std::cerr << "Invalid address.\n";
			closesocket(mSock);
			WSACleanup();
			return false;
		}

		if (connect(mSock, (sockaddr*)&mServerAddr, sizeof(mServerAddr)) == SOCKET_ERROR)
		{
			std::cerr << "Connection failed: " << WSAGetLastError() << "\n";
			closesocket(mSock);
			WSACleanup();
			return false;
		}

		std::cout << "Connected to server.\n";

		mRecvThread = std::make_unique<std::thread>(&CClient::ReceiveThread, this, mSock);
		mHbThread = std::make_unique<std::thread>(&CClient::HeartbeatThread, this, mSock);

		return true;
	}

	void SendMsg(int senderId, int msgType, const void* body, int bodyLen)
	{
		MessageHeader header{ senderId, msgType, bodyLen };

		if (!SendAll(mSock, reinterpret_cast<char*>(&header), sizeof(header)))
			return;

		if (bodyLen > 0 && body)
			SendAll(mSock, reinterpret_cast<const char*>(body), bodyLen);
	}

	bool PollMessage(RecvMessage& out)
	{
		std::lock_guard<std::mutex> lock(mQueueMutex);
		if (mMessageQueue.empty()) return false;
		out = std::move(mMessageQueue.front());
		mMessageQueue.pop();
		return true;
	}

private:
	void ReceiveThread(SOCKET sock)
	{
		while (true)
		{
			MessageHeader header;
			std::vector<char> bodyBuffer;
			if (!ReceiveMsg(sock, header, bodyBuffer))
			{
				std::cout << "Disconnected from server.\n";
				break;
			}

			RecvMessage msg{ header.senderId, header.msgType, std::move(bodyBuffer) };
			std::lock_guard<std::mutex> lock(mQueueMutex);
			mMessageQueue.push(std::move(msg));
		}
	}

	void HeartbeatThread(SOCKET sock)
	{
		while (true)
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			SendMsg(0, (int)ClientMessage::Type::MSG_HEARTBEAT, nullptr, 0);
		}
	}

	bool SendAll(SOCKET sock, const char* data, int length)
	{
		int totalSent = 0;
		while (totalSent < length)
		{
			int sent = send(sock, data + totalSent, length - totalSent, 0);
			if (sent == SOCKET_ERROR) return false;
			totalSent += sent;
		}
		return true;
	}

	bool ReceiveMsg(SOCKET sock, MessageHeader& header, std::vector<char>& bodyBuffer)
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

	bool RecvAll(SOCKET sock, char* buffer, int length)
	{
		int totalReceived = 0;
		while (totalReceived < length)
		{
			int received = recv(sock, buffer + totalReceived, length - totalReceived, 0);
			if (received <= 0) return false;
			totalReceived += received;
		}
		return true;
	}
};

// ───────────────────────────────
// 🧪 Main
// ───────────────────────────────
int main()
{
	CClient client;
	if (!client.Init()) return -1;

	std::thread inputThread([&]()
		{
			while (true)
			{
				std::string input;
				std::getline(std::cin, input);
				if (input == "start")
				{
					client.SendMsg(0, (int)ClientMessage::Type::MSG_START, nullptr, 0);
				}
				else
				{
					try
					{
						float value = std::stof(input);
						client.SendMsg(0, (int)ClientMessage::Type::MSG_FLOAT_DATA, &value, sizeof(float));
					}
					catch (...)
					{
						std::cerr << "Invalid input. Enter 'start' or float.\n";
					}
				}
			}
		});

	// 메시지 처리 루프
	while (true)
	{
		RecvMessage msg;
		if (client.PollMessage(msg))
		{
			if (msg.msgType == (int)ServerMessage::Type::MSG_HEARTBEAT_ACK)
				continue;

			std::cout << "[From " << msg.senderId << "] Type " << msg.msgType << ": ";

			switch (msg.msgType)
			{
			case (int)ServerMessage::Type::MSG_CONNECTED:
			{
				int id;
				memcpy(&id, msg.body.data(), sizeof(int));
				std::cout << "Connected! My ID: " << id;
				break;
			}
			case (int)ServerMessage::Type::MSG_NEW_OWNER:
			{
				int newOwnerId;
				memcpy(&newOwnerId, msg.body.data(), sizeof(int));
				std::cout << "Notice: The current room owner is Client " << newOwnerId;
				break;
			}
			case (int)ServerMessage::Type::MSG_JOIN:
			{
				int id;
				memcpy(&id, msg.body.data(), sizeof(int));
				std::cout << "Client joined: " << id;
				break;
			}
			case (int)ServerMessage::Type::MSG_FLOAT_DATA_ACK:
			{
				float val;
				memcpy(&val, msg.body.data(), sizeof(float));
				std::cout << "Received float: " << val;
				break;
			}
			case (int)ServerMessage::Type::MSG_INFO:
			default:
				std::cout << msg.body.data();
				break;
			}
			std::cout << "\n";
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	inputThread.join();
	return 0;
}
