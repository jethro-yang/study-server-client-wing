// ✅ 클라이언트 전체 코드: 서버의 모든 메시지 수신 및 모든 클라 메시지 송신 기능 포함

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

namespace ClientMessage
{
	enum class Type
	{
		MSG_HEARTBEAT,
		MSG_START,
		MSG_PICK_CHARACTER,
		MSG_PICK_ITEM,
		MSG_PICK_MAP,
		MSG_READY,
		MSG_UNREADY,
		MSG_MOVE_UP,
		MSG_MOVE_DOWN,
		MSG_PLAYER_DEAD
	};
}

namespace ServerMessage
{
	enum class Type
	{
		MSG_CONNECTED,
		MSG_NEW_OWNER,
		MSG_HEARTBEAT_ACK,
		MSG_START_ACK,
		MSG_JOIN,
		MSG_DISCONNECT,
		MSG_CONNECTED_REJECT,
		MSG_ROOM_FULL_INFO,
		MSG_PICK_CHARACTER,
		MSG_PICK_ITEM,
		MSG_PICK_MAP,
		MSG_READY,
		MSG_UNREADY,
		MSG_MOVE_UP,
		MSG_MOVE_DOWN,
		MSG_PLAYER_DEAD,
		MSG_GAME_OVER
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
					client.SendMsg(0, (int)ClientMessage::Type::MSG_START, nullptr, 0);
				else if (input == "ready")
					client.SendMsg(0, (int)ClientMessage::Type::MSG_READY, nullptr, 0);
				else if (input == "unready")
					client.SendMsg(0, (int)ClientMessage::Type::MSG_UNREADY, nullptr, 0);
				else if (input == "up")
					client.SendMsg(0, (int)ClientMessage::Type::MSG_MOVE_UP, nullptr, 0);
				else if (input == "down")
					client.SendMsg(0, (int)ClientMessage::Type::MSG_MOVE_DOWN, nullptr, 0);
				else if (input == "dead")
					client.SendMsg(0, (int)ClientMessage::Type::MSG_PLAYER_DEAD, nullptr, 0);
				else if (input.rfind("map ", 0) == 0)
				{
					int mapId = std::stoi(input.substr(4));
					client.SendMsg(0, (int)ClientMessage::Type::MSG_PICK_MAP, &mapId, sizeof(int));
				}
				else if (input.rfind("char ", 0) == 0)
				{
					int charId = std::stoi(input.substr(5));
					client.SendMsg(0, (int)ClientMessage::Type::MSG_PICK_CHARACTER, &charId, sizeof(int));
				}
				else if (input.rfind("item ", 0) == 0)
				{
					int slot = -1, itemId = -1;
					sscanf_s(input.c_str() + 5, "%d %d", &slot, &itemId);
					int data[2] = { slot, itemId };
					client.SendMsg(0, (int)ClientMessage::Type::MSG_PICK_ITEM, data, sizeof(data));
				}
				else
				{
					std::cout << "[Client] Unknown command.\n";
				}
			}
		});

	while (true)
	{
		RecvMessage msg;
		if (client.PollMessage(msg))
		{
			if (msg.msgType != (int)ServerMessage::Type::MSG_HEARTBEAT_ACK)
			{
				std::cout << "[ServerMsg " << msg.msgType << "] From: " << msg.senderId << ", Size: " << msg.body.size() << "\n";
			}

			if (msg.msgType == (int)ServerMessage::Type::MSG_CONNECTED)
			{
				int myId;
				memcpy(&myId, msg.body.data(), sizeof(int));
				std::cout << "[System] Connected. My ID: " << myId << "\n";
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_NEW_OWNER
				&& msg.body.size() == sizeof(int))
			{
				int newOwnerId;
				memcpy(&newOwnerId, msg.body.data(), sizeof(int));
				std::cout << "[System] New Room Owner is: " << newOwnerId << "\n";
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_CONNECTED_REJECT)
			{
				std::string reason(msg.body.begin(), msg.body.end());
				std::cout << "[System] Connection rejected: " << reason << "\n";
				exit(0);
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_JOIN)
			{
				int id;
				if (msg.body.size() == sizeof(int))
				{
					memcpy(&id, msg.body.data(), sizeof(int));
					std::cout << "[System] Player joined: " << id << "\n";
				}
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_DISCONNECT)
			{
				int id;
				if (msg.body.size() == sizeof(int))
				{
					memcpy(&id, msg.body.data(), sizeof(int));
					std::cout << "[System] Player disconnected: " << id << "\n";
				}
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_GAME_OVER)
			{
				if (!msg.body.empty())
					std::cout << "[Game Over] " << msg.body.data() << "\n";
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_MOVE_UP)
			{
				std::cout << "[Game] Player " << msg.senderId << " moved UP\n";
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_MOVE_DOWN)
			{
				std::cout << "[Game] Player " << msg.senderId << " moved DOWN\n";
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_PLAYER_DEAD)
			{
				std::cout << "[Game] Player " << msg.senderId << " is DEAD\n";
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_ROOM_FULL_INFO
				&& msg.body.size() >= sizeof(int) * 3)
			{
				const char* ptr = msg.body.data();
				int ownerId, mapId, playerCount;
				memcpy(&ownerId, ptr, sizeof(int)); ptr += sizeof(int);
				memcpy(&mapId, ptr, sizeof(int)); ptr += sizeof(int);
				memcpy(&playerCount, ptr, sizeof(int)); ptr += sizeof(int);
				std::cout << "[ROOM_INFO] Owner: " << ownerId << ", Map: " << mapId << ", Players: " << playerCount << "\n";

				for (int i = 0; i < playerCount; ++i)
				{
					int id, items[3];
					bool ready;
					memcpy(&id, ptr, sizeof(int)); ptr += sizeof(int);
					memcpy(&ready, ptr, sizeof(bool)); ptr += sizeof(bool);
					memcpy(items, ptr, sizeof(int) * 3); ptr += sizeof(int) * 3;
					std::cout << "  Player " << id << " - Ready: " << (ready ? "Yes" : "No")
						<< ", Items: [" << items[0] << ", " << items[1] << ", " << items[2] << "]\n";
				}
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_START_ACK)
			{
				std::cout << "[Game] Game Started: " << msg.body.data() << "\n";
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_READY)
			{
				std::cout << "[Game] Player " << msg.senderId << " is READY\n";
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_UNREADY)
			{
				std::cout << "[Game] Player " << msg.senderId << " is UNREADY\n";
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_PICK_MAP
				&& msg.body.size() == sizeof(int))
			{
				int mapId;
				memcpy(&mapId, msg.body.data(), sizeof(int));
				std::cout << "[Game] Player " << msg.senderId << " selected Map ID: " << mapId << "\n";
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_PICK_CHARACTER
				&& msg.body.size() == sizeof(int))
			{
				int charId;
				memcpy(&charId, msg.body.data(), sizeof(int));
				std::cout << "[Game] Player " << msg.senderId << " picked Character: " << charId << "\n";
			}
			else if (msg.msgType == (int)ServerMessage::Type::MSG_PICK_ITEM
				&& msg.body.size() == sizeof(int) * 2)
			{
				int slot, itemId;
				memcpy(&slot, msg.body.data(), sizeof(int));
				memcpy(&itemId, msg.body.data() + sizeof(int), sizeof(int));
				std::cout << "[Game] Player " << msg.senderId << " equipped item " << itemId << " in slot " << slot << "\n";
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	inputThread.join();
	return 0;
}
