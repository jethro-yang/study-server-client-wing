#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <string>
#include <algorithm>
#include <unordered_set>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#define PORT 12345
#define MAX_PLAYERS 5

enum GameState { WAITING, RUNNING };

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
		MSG_HEARTBEAT_ACK,
		MSG_START_ACK,
		MSG_JOIN,
		MSG_DISCONNECT,
		MSG_CONNECTED_REJECT,
		MSG_INFO,
		MSG_NEW_OWNER,
		MSG_CLIENT_LIST,
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

bool sendAll(SOCKET sock, const char* data, int len)
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

bool sendMessage(SOCKET sock, int senderId, int msgType, const void* body, int bodyLen)
{
	MessageHeader header{ senderId, msgType, bodyLen };
	if (!sendAll(sock, (char*)&header, sizeof(header))) return false;
	if (body && bodyLen > 0) return sendAll(sock, (char*)body, bodyLen);
	return true;
}

bool recvAll(SOCKET sock, char* buffer, int len)
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

bool receiveMessage(SOCKET sock, MessageHeader& header, std::vector<char>& body)
{
	if (!recvAll(sock, (char*)&header, sizeof(header))) return false;
	body.resize(header.bodyLen);
	if (header.bodyLen > 0) return recvAll(sock, body.data(), header.bodyLen);
	return true;
}

struct Client
{
	SOCKET sock;
	int id;
	std::thread thread;
	bool isReady = false;
	bool isAlive = true;
};

std::recursive_mutex gMutex;
std::vector<Client*> gClients;
std::unordered_set<int> gDeadPlayers;
GameState gState = WAITING;
int gNextId = 1;
int gRoomOwner = -1;
int gMapId = 0;

void broadcast(int senderId, int msgType, const void* data, int len)
{
	for (auto& c : gClients)
		sendMessage(c->sock, senderId, msgType, data, len);
}

void checkGameOver()
{
	int aliveCount = 0;
	for (auto& c : gClients)
		if (c->isAlive) aliveCount++;

	if (aliveCount == 0)
	{
		gState = WAITING;
		const char* msg = "All players dead. Game over.";
		broadcast(0, (int)ServerMessage::Type::MSG_GAME_OVER, msg, strlen(msg) + 1);
		std::cout << "[Server] Game over.\n";
	}
}

void clientThread(Client* client)
{
	while (true)
	{
		MessageHeader header;
		std::vector<char> body;

		if (!receiveMessage(client->sock, header, body))
			break;

		std::lock_guard<std::recursive_mutex> lock(gMutex);

		switch ((ClientMessage::Type)header.msgType)
		{
		case ClientMessage::Type::MSG_HEARTBEAT:
			sendMessage(client->sock, client->id, (int)ServerMessage::Type::MSG_HEARTBEAT_ACK, nullptr, 0);
			break;

		case ClientMessage::Type::MSG_START:
			if (client->id == gRoomOwner)
			{
				gState = RUNNING;
				for (auto& c : gClients) c->isAlive = true;
				gDeadPlayers.clear();
				const char* startMsg = "Game Started!";
				broadcast(client->id, (int)ServerMessage::Type::MSG_START_ACK, startMsg, strlen(startMsg) + 1);
				std::cout << "[Server] Game started by host " << client->id << "\n";
			}
			break;

		case ClientMessage::Type::MSG_READY:
			client->isReady = true;
			broadcast(client->id, (int)ServerMessage::Type::MSG_READY, nullptr, 0);
			std::cout << "[Server] ClientMessage::Type::MSG_READY " << client->id << "\n";
			break;

		case ClientMessage::Type::MSG_UNREADY:
			client->isReady = false;
			broadcast(client->id, (int)ServerMessage::Type::MSG_UNREADY, nullptr, 0);
			std::cout << "[Server] ClientMessage::Type::MSG_UNREADY " << client->id << "\n";
			break;

		case ClientMessage::Type::MSG_PLAYER_DEAD:
			client->isAlive = false;
			broadcast(client->id, (int)ServerMessage::Type::MSG_PLAYER_DEAD, nullptr, 0);
			std::cout << "[Server] ClientMessage::Type::MSG_PLAYER_DEAD " << client->id << "\n";
			gDeadPlayers.insert(client->id);
			checkGameOver();
			break;

		case ClientMessage::Type::MSG_PICK_MAP:
		{
			if (client->id == gRoomOwner && header.bodyLen == sizeof(int))
			{
				memcpy(&gMapId, body.data(), sizeof(int));
				broadcast(0, (int)ServerMessage::Type::MSG_PICK_MAP, &gMapId, sizeof(int));
				std::cout << "[Server] Map changed to: " << gMapId << "\n";
			}
			break;
		}
		case ClientMessage::Type::MSG_MOVE_UP:
			broadcast(client->id, (int)ServerMessage::Type::MSG_MOVE_UP, nullptr, 0);
			std::cout << "[Server] ClientMessage::Type::MSG_MOVE_UP " << client->id << "\n";
			break;

		case ClientMessage::Type::MSG_MOVE_DOWN:
			broadcast(client->id, (int)ServerMessage::Type::MSG_MOVE_DOWN, nullptr, 0);
			std::cout << "[Server] ClientMessage::Type::MSG_MOVE_DOWN " << client->id << "\n";
			break;

		case ClientMessage::Type::MSG_PICK_ITEM:
		{
			if (header.bodyLen == sizeof(int) * 2)
			{
				broadcast(client->id, (int)ServerMessage::Type::MSG_PICK_ITEM, body.data(), header.bodyLen);

				int slot, itemId;
				memcpy(&slot, body.data(), sizeof(int));
				memcpy(&itemId, body.data() + sizeof(int), sizeof(int));
				std::cout << "[Server] Client " << client->id << " picked item " << itemId << " in slot " << slot << "\n";
			}
			break;
		}
		case ClientMessage::Type::MSG_PICK_CHARACTER:
		{
			if (header.bodyLen == sizeof(int))
			{
				broadcast(client->id, (int)ServerMessage::Type::MSG_PICK_CHARACTER, body.data(), sizeof(int));

				int characterId;
				memcpy(&characterId, body.data(), sizeof(int));
				std::cout << "[Server] Client " << client->id << " picked character " << characterId << "\n";
			}
			break;
		}

		default:
			break;
		}
	}

	// --- 클라이언트 종료 처리 ---
	{
		std::lock_guard<std::recursive_mutex> lock(gMutex);
		auto it = std::find_if(gClients.begin(), gClients.end(), [client](Client* c) { return c->id == client->id; });

		if (it != gClients.end())
		{
			bool wasOwner = (client->id == gRoomOwner);
			gClients.erase(it);
			std::cout << "[Server] Removed Client " << client->id << "\n";

			broadcast(client->id, (int)ServerMessage::Type::MSG_DISCONNECT, &client->id, sizeof(int));

			if (gClients.empty())
			{
				gRoomOwner = -1;
				std::cout << "[Server] No clients left. Room owner reset.\n";
			}
			else if (wasOwner)
			{
				// 새로운 방장 설정 및 브로드캐스트
				gRoomOwner = gClients.front()->id;
				std::cout << "[Server] Room owner changed to Client " << gRoomOwner << "\n";
				broadcast(0, (int)ServerMessage::Type::MSG_NEW_OWNER, &gRoomOwner, sizeof(gRoomOwner));
			}
		}
	}

	closesocket(client->sock);
	delete client;
}

void sendClientList(Client* newClient)
{
	std::vector<int> ids;
	for (auto& c : gClients)
	{
		if (c->id != newClient->id)
			ids.push_back(c->id);
	}

	if (!ids.empty())
	{
		int bodyLen = sizeof(int) * ids.size();
		sendMessage(newClient->sock, 0, (int)ServerMessage::Type::MSG_CLIENT_LIST, ids.data(), bodyLen);
		std::cout << "[Server] Sent client list to " << newClient->id << ": [ ";
		for (int id : ids) std::cout << id << " ";
		std::cout << "]\n";
	}
}

int main()
{
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(PORT);
	bind(server, (sockaddr*)&addr, sizeof(addr));
	listen(server, SOMAXCONN);

	std::cout << "[Server] Listening on port " << PORT << "...\n";

	while (true)
	{
		sockaddr_in clientAddr{};
		int size = sizeof(clientAddr);
		SOCKET clientSock = accept(server, (sockaddr*)&clientAddr, &size);

		// 인원 제한 확인
		std::lock_guard<std::recursive_mutex> lock(gMutex);
		if ((int)gClients.size() >= MAX_PLAYERS)
		{
			const char* fullMsg = "Room is full. Connection rejected.";
			sendMessage(clientSock, 0, (int)ServerMessage::Type::MSG_CONNECTED_REJECT, fullMsg, strlen(fullMsg) + 1);
			std::cout << "[Server] Rejected connection: room full.\n";
			closesocket(clientSock);
			continue;
		}

		Client* c = new Client;
		c->sock = clientSock;
		c->id = gNextId++;

		{
			std::lock_guard<std::recursive_mutex> lock(gMutex);
			gClients.push_back(c);
			if (gRoomOwner == -1) gRoomOwner = c->id;

			std::cout << "[Server] ServerMessage::Type::MSG_CONNECTED " << c->id << "\n";
			std::cout << "[Server] ServerMessage::Type::MSG_NEW_OWNER " << gRoomOwner << "\n";
			// 새 클라이언트에게 자신의 id 및 방장 정보 전송
			sendMessage(clientSock, c->id, (int)ServerMessage::Type::MSG_CONNECTED, &c->id, sizeof(int));
			sendMessage(clientSock, 0, (int)ServerMessage::Type::MSG_NEW_OWNER, &gRoomOwner, sizeof(int));

			// 새 클라이언트에게 현재 접속자 리스트 전송
			sendClientList(c);

			// 기존 클라이언트들에게 새로운 클라이언트의 접속 알림
			for (auto& other : gClients)
			{
				if (other->id != c->id)
					sendMessage(other->sock, c->id, (int)ServerMessage::Type::MSG_JOIN, &c->id, sizeof(int));
			}
		}

		c->thread = std::thread(clientThread, c);
		c->thread.detach();
	}

	closesocket(server);
	WSACleanup();
	return 0;
}
