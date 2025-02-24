#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <string>
#include <algorithm>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <cstring>
#pragma comment(lib, "ws2_32.lib")

// 설정값
#define PORT 12345
#define TARGET_NUM 100.0f      // 승리 조건 float 값
#define MAX_PLAYERS 5          // 최대 접속 클라이언트 수

// 게임 상태
enum GameState
{
	WAITING,  // 게임 시작 전 대기 상태
	RUNNING   // 게임 진행 중
};

// 메시지 타입 enum
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

// 메시지 헤더 구조체 (패킹)
#pragma pack(push, 1)
struct MessageHeader
{
	int senderId;   // 발신자 ID
	int msgType;    // 메시지 타입
	int bodyLen;    // 바디의 길이 (바이트 단위)
};
#pragma pack(pop)

// --- Helper Functions ---

// 보내기: 데이터를 length만큼 보내기 (반드시 전송될 때까지 반복)
bool sendAll(SOCKET sock, const char* data, int length)
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

// 메시지 전송 함수: header와 body를 연속해서 전송
bool sendMessage(SOCKET sock, int senderId, int msgType, const void* body, int bodyLen)
{
	MessageHeader header;
	header.senderId = senderId;
	header.msgType = msgType;
	header.bodyLen = bodyLen;

	// 헤더 전송
	if (!sendAll(sock, reinterpret_cast<char*>(&header), sizeof(header)))
		return false;

	// 바디 전송 (bodyLen > 0이면)
	if (bodyLen > 0 && body != nullptr)
	{
		if (!sendAll(sock, reinterpret_cast<const char*>(body), bodyLen))
			return false;
	}
	return true;
}

// 수신: length 바이트만큼 받기
bool recvAll(SOCKET sock, char* buffer, int length)
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

// 메시지 수신: 헤더를 먼저 받고, header.bodyLen만큼 바디를 받는다.
bool receiveMessage(SOCKET sock, MessageHeader& header, std::vector<char>& bodyBuffer)
{
	if (!recvAll(sock, reinterpret_cast<char*>(&header), sizeof(header)))
		return false;

	bodyBuffer.resize(header.bodyLen);

	if (header.bodyLen > 0)
	{
		if (!recvAll(sock, bodyBuffer.data(), header.bodyLen))
			return false;
	}

	return true;
}

// --- 서버 게임 관련 코드 ---

// 클라이언트 정보 구조체
struct Client
{
	SOCKET sock;
	int id;
	std::thread thread;
};

// std::recursive_mutex를 사용하여 재진입 락을 허용
std::recursive_mutex clientsMutex;
std::vector<Client*> clients;
GameState gameState = WAITING;
int roomOwnerId = -1;       // 방장 클라이언트의 id
int nextClientId = 1;       // 신규 접속 시 부여할 id
int winnerId = -1;          // 현재 라운드 승자의 id (없으면 -1)

// 모든 클라이언트에 메시지를 브로드캐스트
void broadcastMessage(int senderId, int msgType, const void* body, int bodyLen)
{
	std::lock_guard<std::recursive_mutex> lock(clientsMutex);

	for (auto client : clients)
	{
		sendMessage(client->sock, senderId, msgType, body, bodyLen);
	}
}

// 게임 중 플레이어 수가 2명 미만이면 게임 중단
void checkAndAbortGameIfNotEnoughPlayers()
{
	if (clients.size() < 2 && gameState == RUNNING)
	{
		const char* info = "Game aborted: Not enough players.";
		broadcastMessage(0, (int)ServerMessage::Type::MSG_INFO, info, (int)strlen(info) + 1);
		gameState = WAITING;
	}
}

// 클라이언트 처리 스레드
void clientThread(Client* client)
{
	while (true)
	{
		MessageHeader header;
		std::vector<char> bodyBuffer;
		if (!receiveMessage(client->sock, header, bodyBuffer))
		{
			std::cout << "Client " << client->id << " disconnected." << "\n";
			break;
		}
		{
			std::lock_guard<std::recursive_mutex> lock(clientsMutex);
			switch (header.msgType)
			{
			case (int)ClientMessage::Type::MSG_HEARTBEAT:
			{
				// 하트비트 수신: 즉시 응답
				sendMessage(client->sock, client->id, (int)ServerMessage::Type::MSG_HEARTBEAT_ACK, nullptr, 0);
				break;
			}
			case (int)ClientMessage::Type::MSG_START:
			{
				// 방장만 게임 시작 요청 가능 (최소 2명 이상이어야 함)
				if (client->id == roomOwnerId)
				{
					if (clients.size() >= 2)
					{
						gameState = RUNNING;
						winnerId = -1;
						const char* startMsg = "Game started! Send your float numbers.";
						broadcastMessage(client->id, (int)ServerMessage::Type::MSG_START_ACK, startMsg, (int)strlen(startMsg) + 1);
						std::cout << "Game started by room owner (Client " << client->id << ")." << "\n";
					}
					else
					{
						const char* reply = "Not enough players to start the game.";
						sendMessage(client->sock, 0, (int)ServerMessage::Type::MSG_INFO, reply, (int)strlen(reply) + 1);
					}
				}
				break;
			}
			case (int)ClientMessage::Type::MSG_FLOAT_DATA:
			{
				// 실수 데이터: 바디는 sizeof(float)여야 함
				if (header.bodyLen == sizeof(float))
				{
					float value;
					memcpy(&value, bodyBuffer.data(), sizeof(float));
					std::cout << "Client " << client->id << " sent float: " << value << "\n";
					// 모든 클라이언트에 전달 (브로드캐스트)
					broadcastMessage(client->id, (int)ServerMessage::Type::MSG_FLOAT_DATA_ACK, &value, sizeof(float));

					// 승리 조건 검사
					if (value >= TARGET_NUM && winnerId == -1)
					{
						winnerId = client->id;
						std::string winInfo = "Game round ended. Winner is Client " + std::to_string(winnerId) + ".";
						broadcastMessage(0, (int)ServerMessage::Type::MSG_INFO, winInfo.c_str(), (int)(winInfo.size() + 1));

						// 각 클라이언트에게 승/패 메시지 전송
						for (auto c : clients)
						{
							std::string result;
							if (c->id == winnerId)
							{
								result = "Stage ended. You are the winner! (Winner: Client " + std::to_string(winnerId) + ")";
							}
							else
							{
								result = "Stage ended. You lost. (Winner: Client " + std::to_string(winnerId) + ")";
							}
							sendMessage(c->sock, 0, (int)ServerMessage::Type::MSG_INFO, result.c_str(), (int)(result.size() + 1));
						}
						std::cout << "Game round ended. Winner is Client " << winnerId << "." << "\n";
						gameState = WAITING;
					}
				}
				break;
			}
			default:
			{
				std::cout << "Received unknown message type from Client " << client->id << "\n";
				break;
			}
			}
		}
	}
	// 클라이언트 연결 종료 처리
	{
		std::lock_guard<std::recursive_mutex> lock(clientsMutex);
		auto it = std::find_if(clients.begin(), clients.end(), [client](Client* c) { return c->id == client->id; });

		if (it != clients.end())
		{
			// 나간 클라가 방장이었다면?
			bool wasRoomOwner = (client->id == roomOwnerId);
			clients.erase(it);
			std::cout << "Removed Client " << client->id << "." << "\n";
			const char* discMsg = "has left the room.";
			broadcastMessage(client->id, (int)ServerMessage::Type::MSG_DISCONNECT, discMsg, (int)strlen(discMsg) + 1);

			if (clients.empty())
			{
				roomOwnerId = -1;
			}
			else
			{
				if (wasRoomOwner)
				{
					roomOwnerId = clients.front()->id;
					std::string newOwnerMsg = "Room owner left. New room owner is Client " + std::to_string(roomOwnerId) + ".";
					broadcastMessage(0, (int)ServerMessage::Type::MSG_INFO, newOwnerMsg.c_str(), (int)(newOwnerMsg.size() + 1));
					broadcastMessage(0, (int)ServerMessage::Type::MSG_NEW_OWNER, &roomOwnerId, sizeof(roomOwnerId));
					std::cout << "Room owner changed to Client " << roomOwnerId << "." << "\n";
				}
			}
			checkAndAbortGameIfNotEnoughPlayers();
		}
	}
	closesocket(client->sock);
	delete client;
}

int main()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		std::cerr << "WSAStartup failed." << "\n";
		return 1;
	}

	SOCKET serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (serverSock == INVALID_SOCKET)
	{
		std::cerr << "Socket creation failed: " << WSAGetLastError() << "\n";
		WSACleanup();
		return 1;
	}

	int opt = 1;

	if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) == SOCKET_ERROR)
	{
		std::cerr << "setsockopt failed: " << WSAGetLastError() << "\n";
		closesocket(serverSock);
		WSACleanup();
		return 1;
	}

	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(PORT);

	if (bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		std::cerr << "Bind failed: " << WSAGetLastError() << "\n";
		closesocket(serverSock);
		WSACleanup();
		return 1;
	}

	if (listen(serverSock, SOMAXCONN) == SOCKET_ERROR)
	{
		std::cerr << "Listen failed: " << WSAGetLastError() << "\n";
		closesocket(serverSock);
		WSACleanup();
		return 1;
	}
	std::cout << "Server is running. Waiting for clients..." << "\n";

	while (true)
	{
		sockaddr_in clientAddr;
		int clientAddrSize = sizeof(clientAddr);
		SOCKET clientSock = accept(serverSock, (sockaddr*)&clientAddr, &clientAddrSize);

		if (clientSock == INVALID_SOCKET)
		{
			std::cerr << "Accept failed: " << WSAGetLastError() << "\n";
			continue;
		}

		Client* newClient = new Client;
		newClient->sock = clientSock;
		newClient->id = nextClientId++;

		{
			std::lock_guard<std::recursive_mutex> lock(clientsMutex);

			if (clients.size() >= MAX_PLAYERS)
			{
				const char* fullMsg = "Room is full. Connection rejected.";
				sendMessage(newClient->sock, 0, (int)ServerMessage::Type::MSG_INFO, fullMsg, (int)strlen(fullMsg) + 1);
				closesocket(newClient->sock);
				delete newClient;
				continue;
			}
			clients.push_back(newClient);

			sendMessage(newClient->sock, newClient->id, (int)ServerMessage::Type::MSG_CONNECTED, &newClient->id, sizeof(newClient->id));

			if (roomOwnerId == -1)
			{
				roomOwnerId = newClient->id;
				const char* ownerMsg = "You are the room owner.";
				sendMessage(newClient->sock, newClient->id, (int)ServerMessage::Type::MSG_INFO, ownerMsg, (int)strlen(ownerMsg) + 1);
			}
			else
			{
				std::string joinMsg = "Connected to server. Room owner is Client " + std::to_string(roomOwnerId) + ".";
				sendMessage(newClient->sock, newClient->id, (int)ServerMessage::Type::MSG_INFO, joinMsg.c_str(), (int)(joinMsg.size() + 1));
			}

			std::cout << "Client " << newClient->id << " connected." << "\n";
			std::string broadcastJoin = "New client joined.";
			broadcastMessage(newClient->id, (int)ServerMessage::Type::MSG_INFO, broadcastJoin.c_str(), (int)(broadcastJoin.size() + 1));
			broadcastMessage(newClient->id, (int)ServerMessage::Type::MSG_JOIN, &newClient->id, sizeof(newClient->id));
		}

		newClient->thread = std::thread(clientThread, newClient);
		newClient->thread.detach();
	}

	closesocket(serverSock);
	WSACleanup();
	return 0;
}
