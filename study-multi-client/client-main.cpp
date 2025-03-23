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
		MSG_PICK_CHARACTER,
		MSG_PICK_ITEM,
		MSG_READY,
		MSG_UNREADY,
		MSG_MOVE_UP,
		MSG_MOVE_DOWN
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
		MSG_CLIENT_LIST,
		MSG_PICK_CHARACTER,
		MSG_PICK_ITEM,
		MSG_READY,
		MSG_UNREADY,
		MSG_MOVE_UP,
		MSG_MOVE_DOWN
	};
}

#pragma pack(push, 1)
struct MessageHeader
{
	int senderId;
	int msgType;
	int bodyLen;
};

struct ItemSelectInfo
{
	int slotIndex;
	int itemId;
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
			std::cerr << "Socket creation failed.\n";
			return false;
		}

		mServerAddr.sin_family = AF_INET;
		mServerAddr.sin_port = htons(PORT);
		inet_pton(AF_INET, SERVER_IP, &mServerAddr.sin_addr);

		if (connect(mSock, (sockaddr*)&mServerAddr, sizeof(mServerAddr)) == SOCKET_ERROR)
		{
			std::cerr << "Connect failed.\n";
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
		SendAll(mSock, reinterpret_cast<const char*>(&header), sizeof(header));
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
			std::lock_guard<std::mutex> lock(mQueueMutex);
			mMessageQueue.push({ header.senderId, header.msgType, std::move(bodyBuffer) });
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
			return RecvAll(sock, bodyBuffer.data(), header.bodyLen);
		return true;
	}

	bool RecvAll(SOCKET sock, char* buffer, int length)
	{
		int total = 0;
		while (total < length)
		{
			int received = recv(sock, buffer + total, length - total, 0);
			if (received <= 0) return false;
			total += received;
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
				else if (input == "ready")
				{
					client.SendMsg(0, (int)ClientMessage::Type::MSG_READY, nullptr, 0);
				}
				else if (input == "unready")
				{
					client.SendMsg(0, (int)ClientMessage::Type::MSG_UNREADY, nullptr, 0);
				}
				else if (input == "q")
				{
					client.SendMsg(0, (int)ClientMessage::Type::MSG_MOVE_UP, nullptr, 0);
				}
				else if (input == "w")
				{
					client.SendMsg(0, (int)ClientMessage::Type::MSG_MOVE_DOWN, nullptr, 0);
				}
				else if (input.rfind("char ", 0) == 0)
				{
					int charId = std::stoi(input.substr(5));
					client.SendMsg(0, (int)ClientMessage::Type::MSG_PICK_CHARACTER, &charId, sizeof(int));
				}
				else if (input.rfind("item ", 0) == 0)
				{
					int slot, item;
					scanf_s(input.c_str(), "item %d %d", &slot, &item);
					ItemSelectInfo info{ slot, item };
					client.SendMsg(0, (int)ClientMessage::Type::MSG_PICK_ITEM, &info, sizeof(info));
				}
				else
				{
					std::cerr << "Unknown command.\n";
				}
			}
		});

	while (true)
	{
		RecvMessage msg;
		if (client.PollMessage(msg))
		{
			if (msg.msgType != (int)ServerMessage::Type::MSG_HEARTBEAT_ACK)
				std::cout << "[MSG TYPE: " << msg.msgType << " ] ";

			switch (msg.msgType)
			{
			case (int)ServerMessage::Type::MSG_CONNECTED:
			{
				int id;
				memcpy(&id, msg.body.data(), sizeof(int));
				std::cout << "My ID: " << id << "\n";
				break;
			}
			case (int)ServerMessage::Type::MSG_NEW_OWNER:
			{
				int id;
				memcpy(&id, msg.body.data(), sizeof(int));
				std::cout << "Room owner is now Client " << id << "\n";
				break;
			}
			case (int)ServerMessage::Type::MSG_CLIENT_LIST:
			{
				int count;
				memcpy(&count, msg.body.data(), sizeof(int));
				std::cout << "Current players (" << count << "): ";
				for (int i = 1; i <= count; ++i)
				{
					int id;
					memcpy(&id, msg.body.data() + sizeof(int) * i, sizeof(int));
					std::cout << id << " ";
				}
				std::cout << "\n";
				break;
			}
			case (int)ServerMessage::Type::MSG_PICK_CHARACTER:
			{
				int charId;
				memcpy(&charId, msg.body.data(), sizeof(int));
				std::cout << "Client " << msg.senderId << " picked character " << charId << "\n";
				break;
			}
			case (int)ServerMessage::Type::MSG_PICK_ITEM:
			{
				ItemSelectInfo info;
				memcpy(&info, msg.body.data(), sizeof(info));
				std::cout << "Client " << msg.senderId << " picked item " << info.itemId << " at slot " << info.slotIndex << "\n";
				break;
			}
			case (int)ServerMessage::Type::MSG_READY:
				std::cout << "Client " << msg.senderId << " is READY\n"; break;
			case (int)ServerMessage::Type::MSG_UNREADY:
				std::cout << "Client " << msg.senderId << " is UNREADY\n"; break;
			case (int)ServerMessage::Type::MSG_MOVE_UP:
				std::cout << "Client " << msg.senderId << " moved UP\n"; break;
			case (int)ServerMessage::Type::MSG_MOVE_DOWN:
				std::cout << "Client " << msg.senderId << " moved DOWN\n"; break;
			case (int)ServerMessage::Type::MSG_DISCONNECT:

			{
				int id;
				memcpy(&id, msg.body.data(), sizeof(int));
				std::cout << "Client " << id << " disconnected\n";
				break;
			}
			case (int)ServerMessage::Type::MSG_INFO:
				std::cout << msg.body.data() << "\n"; break;
			default:
				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	inputThread.join();
	return 0;
}
