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
#pragma comment(lib, "ws2_32.lib")

#define PORT 12345
#define SERVER_IP "127.0.0.1"

namespace ClientMessage {
	enum class Type {
		MSG_HEARTBEAT,			// 0
		MSG_START,				// 1
		MSG_PICK_CHARACTER,		// 2
		MSG_PICK_ITEM,			// 3
		MSG_PICK_MAP,			// 4
		MSG_READY,				// 5
		MSG_UNREADY,			// 6
		MSG_MOVE_UP,			// 7
		MSG_MOVE_DOWN,			// 8
		MSG_PLAYER_DEAD			// 9
	};
}

namespace ServerMessage {
	enum class Type {
		MSG_CONNECTED,		// 0
		MSG_HEARTBEAT_ACK,	// 1
		MSG_START_ACK,		// 2
		MSG_JOIN,			// 3
		MSG_DISCONNECT,		// 4
		MSG_INFO,			// 5
		MSG_NEW_OWNER,		// 6
		MSG_CLIENT_LIST,	// 7
		MSG_PICK_CHARACTER,	// 8
		MSG_PICK_ITEM,		// 9
		MSG_PICK_MAP,		// 10
		MSG_READY,			// 11
		MSG_UNREADY,		// 12
		MSG_MOVE_UP,		// 13
		MSG_MOVE_DOWN,		// 14
		MSG_PLAYER_DEAD,	// 15
		MSG_GAME_OVER		// 16
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
		if (WSAStartup(MAKEWORD(2, 2), &mWsaData) != 0) return false;

		mSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (mSock == INVALID_SOCKET) return false;

		mServerAddr.sin_family = AF_INET;
		mServerAddr.sin_port = htons(PORT);
		inet_pton(AF_INET, SERVER_IP, &mServerAddr.sin_addr);

		if (connect(mSock, (sockaddr*)&mServerAddr, sizeof(mServerAddr)) == SOCKET_ERROR) return false;

		std::cout << "Connected to server.\n";

		mRecvThread = std::make_unique<std::thread>(&CClient::ReceiveThread, this, mSock);
		mHbThread = std::make_unique<std::thread>(&CClient::HeartbeatThread, this, mSock);

		return true;
	}

	void SendMsg(int senderId, int msgType, const void* body, int bodyLen)
	{
		MessageHeader header{ senderId, msgType, bodyLen };
		SendAll(mSock, (char*)&header, sizeof(header));
		if (body && bodyLen > 0) SendAll(mSock, (char*)body, bodyLen);
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

	bool SendAll(SOCKET sock, const char* data, int len)
	{
		int sent = 0;
		while (sent < len)
		{
			int r = send(sock, data + sent, len - sent, 0);
			if (r == SOCKET_ERROR) return false;
			sent += r;
		}
		return true;
	}

	bool ReceiveMsg(SOCKET sock, MessageHeader& header, std::vector<char>& bodyBuffer)
	{
		if (!RecvAll(sock, (char*)&header, sizeof(header))) return false;
		bodyBuffer.resize(header.bodyLen);
		if (header.bodyLen > 0)
			return RecvAll(sock, bodyBuffer.data(), header.bodyLen);
		return true;
	}

	bool RecvAll(SOCKET sock, char* buffer, int len)
	{
		int recvd = 0;
		while (recvd < len)
		{
			int r = recv(sock, buffer + recvd, len - recvd, 0);
			if (r <= 0) return false;
			recvd += r;
		}
		return true;
	}
};

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
				else if (input == "dead")
				{
					client.SendMsg(0, (int)ClientMessage::Type::MSG_PLAYER_DEAD, nullptr, 0);
				}
				// 여기서부터는 데이터가 껴들어감.
				else if (input.rfind("map ", 0) == 0)
				{
					try
					{
						int mapId = std::stoi(input.substr(4));
						client.SendMsg(0, (int)ClientMessage::Type::MSG_PICK_MAP, &mapId, sizeof(int));
					}
					catch (...)
					{
						std::cout << "Invalid map number.\n";
					}
				}
				else if (input.rfind("char ", 0) == 0)
				{
					try
					{
						int characterId = std::stoi(input.substr(5));
						client.SendMsg(0, (int)ClientMessage::Type::MSG_PICK_CHARACTER, &characterId, sizeof(int));
					}
					catch (...)
					{
						std::cout << "Invalid char number.\n";
					}
				}
				else if (input.rfind("item ", 0) == 0)
				{
					int slot = -1, itemId = -1;

					// C-string을 만들기 위해 복사
					char buf[256] = {};
					strncpy_s(buf, input.c_str() + 5, sizeof(buf) - 1);

					if (sscanf_s(buf, "%d %d", &slot, &itemId) == 2)
					{
						int data[2] = { slot, itemId };
						client.SendMsg(0, (int)ClientMessage::Type::MSG_PICK_ITEM, data, sizeof(data));
					}
					else
					{
						std::cout << "Invalid format. Use: item <slot> <itemId>\n";
					}
				}


				else
				{
					std::cout << "Unknown command.\n";
				}
			}
		});

	// 메시지 처리 루프
	while (true)
	{
		RecvMessage msg;
		if (client.PollMessage(msg))
		{
			switch (msg.msgType)
			{
			case (int)ServerMessage::Type::MSG_CONNECTED:
			{
				int id;
				memcpy(&id, msg.body.data(), sizeof(int));
				std::cout << "[System " << msg.msgType << "] Connected. My ID: " << id << "\n";
				break;
			}
			case (int)ServerMessage::Type::MSG_NEW_OWNER:
			{
				int id;
				memcpy(&id, msg.body.data(), sizeof(int));
				std::cout << "[System " << msg.msgType << "] New Room Owner: " << id << "\n";
				break;
			}
			case (int)ServerMessage::Type::MSG_PLAYER_DEAD:
				std::cout << "[Game " << msg.msgType << "] Player " << msg.senderId << " died.\n";
				break;

			case (int)ServerMessage::Type::MSG_PICK_MAP:
			{
				if (msg.body.size() >= sizeof(int)) {
					int mapId;
					memcpy(&mapId, msg.body.data(), sizeof(int));
					std::cout << "[Game " << msg.msgType << "] Map changed to " << mapId << "\n";
				}
				break;
			}

			case (int)ServerMessage::Type::MSG_PICK_ITEM:
			{
				if (msg.body.size() >= sizeof(int) * 2) {
					int slot, itemId;
					memcpy(&slot, msg.body.data(), sizeof(int));
					memcpy(&itemId, msg.body.data() + sizeof(int), sizeof(int));
					std::cout << "[Game " << msg.msgType << "] Player " << msg.senderId << " picked item " << itemId << " in slot " << slot << "\n";
				}
				break;
			}

			case (int)ServerMessage::Type::MSG_PICK_CHARACTER:
			{
				if (msg.body.size() >= sizeof(int)) {
					int characterId;
					memcpy(&characterId, msg.body.data(), sizeof(int));
					std::cout << "[Game " << msg.msgType << "] Player " << msg.senderId << " picked character " << characterId << "\n";
				}
				break;
			}
			case (int)ServerMessage::Type::MSG_CLIENT_LIST:
			{
				std::cout << "[System " << msg.msgType << "] Client list received: ";
				int count = msg.body.size() / sizeof(int);
				for (int i = 0; i < count; ++i) {
					int id;
					memcpy(&id, msg.body.data() + i * sizeof(int), sizeof(int));
					std::cout << id << " ";
				}
				std::cout << "\n";
				break;
			}
			case (int)ServerMessage::Type::MSG_JOIN:
			{
				if (msg.body.size() >= sizeof(int)) {
					int newId;
					memcpy(&newId, msg.body.data(), sizeof(int));
					std::cout << "[System " << msg.msgType << "] New client joined: " << newId << "\n";
				}
				break;
			}
			case (int)ServerMessage::Type::MSG_GAME_OVER:
				std::cout << "[Game " << msg.msgType << "] " << msg.body.data() << "\n";
				break;

			case (int)ServerMessage::Type::MSG_MOVE_UP:
				std::cout << "[Game] Client " << msg.senderId << " moved UP\n";
				break;

			case (int)ServerMessage::Type::MSG_MOVE_DOWN:
				std::cout << "[Game] Client " << msg.senderId << " moved DOWN\n";
				break;

			case (int)ServerMessage::Type::MSG_INFO:
				std::cout << "[Info " << msg.msgType << "] " << msg.body.data() << "\n";
				break;

			default:
				if (msg.msgType != (int)ServerMessage::Type::MSG_HEARTBEAT_ACK)
					std::cout << "[MSG " << msg.msgType << "] From " << msg.senderId << "\n";
				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	inputThread.join();
	return 0;
}
