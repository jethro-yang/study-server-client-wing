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

#define PORT 12345
#define TARGET_NUM 100.0f
#define MAX_PLAYERS 5

enum GameState
{
    WAITING,
    RUNNING
};

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
        MSG_CLIENT_LIST
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

bool sendMessage(SOCKET sock, int senderId, int msgType, const void* body, int bodyLen)
{
    MessageHeader header{ senderId, msgType, bodyLen };
    if (!sendAll(sock, reinterpret_cast<char*>(&header), sizeof(header)))
        return false;
    if (bodyLen > 0 && body != nullptr)
        return sendAll(sock, reinterpret_cast<const char*>(body), bodyLen);
    return true;
}

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

bool receiveMessage(SOCKET sock, MessageHeader& header, std::vector<char>& bodyBuffer)
{
    if (!recvAll(sock, reinterpret_cast<char*>(&header), sizeof(header)))
        return false;

    bodyBuffer.resize(header.bodyLen);
    if (header.bodyLen > 0)
        return recvAll(sock, bodyBuffer.data(), header.bodyLen);

    return true;
}

struct Client
{
    SOCKET sock;
    int id;
    std::thread thread;
};

std::recursive_mutex clientsMutex;
std::vector<Client*> clients;
GameState gameState = WAITING;
int roomOwnerId = -1;
int nextClientId = 1;
int winnerId = -1;

void broadcastMessage(int senderId, int msgType, const void* body, int bodyLen)
{
    std::lock_guard<std::recursive_mutex> lock(clientsMutex);
    for (auto client : clients)
        sendMessage(client->sock, senderId, msgType, body, bodyLen);
}

void checkAndAbortGameIfNotEnoughPlayers()
{
    if (clients.size() < 2 && gameState == RUNNING)
    {
        const char* info = "Game aborted: Not enough players.";
        broadcastMessage(0, (int)ServerMessage::Type::MSG_INFO, info, (int)strlen(info) + 1);
        gameState = WAITING;
    }
}

void clientThread(Client* client)
{
    while (true)
    {
        MessageHeader header;
        std::vector<char> bodyBuffer;
        if (!receiveMessage(client->sock, header, bodyBuffer))
        {
            std::cout << "Client " << client->id << " disconnected.\n";
            break;
        }

        std::lock_guard<std::recursive_mutex> lock(clientsMutex);
        switch (header.msgType)
        {
        case (int)ClientMessage::Type::MSG_HEARTBEAT:
            sendMessage(client->sock, client->id, (int)ServerMessage::Type::MSG_HEARTBEAT_ACK, nullptr, 0);
            break;

        case (int)ClientMessage::Type::MSG_START:
            if (client->id == roomOwnerId && clients.size() >= 2)
            {
                gameState = RUNNING;
                winnerId = -1;
                const char* startMsg = "Game started! Send your float numbers.";
                broadcastMessage(client->id, (int)ServerMessage::Type::MSG_START_ACK, startMsg, (int)strlen(startMsg) + 1);
                std::cout << "Game started by Client " << client->id << "\n";
            }
            else
            {
                const char* reply = "Not enough players to start the game.";
                sendMessage(client->sock, 0, (int)ServerMessage::Type::MSG_INFO, reply, (int)strlen(reply) + 1);
            }
            break;

        case (int)ClientMessage::Type::MSG_FLOAT_DATA:
            if (header.bodyLen == sizeof(float))
            {
                float value;
                memcpy(&value, bodyBuffer.data(), sizeof(float));
                std::cout << "Client " << client->id << " sent float: " << value << "\n";
                broadcastMessage(client->id, (int)ServerMessage::Type::MSG_FLOAT_DATA_ACK, &value, sizeof(float));

                if (value >= TARGET_NUM && winnerId == -1)
                {
                    winnerId = client->id;
                    std::string winInfo = "Game round ended. Winner is Client " + std::to_string(winnerId) + ".";
                    broadcastMessage(0, (int)ServerMessage::Type::MSG_INFO, winInfo.c_str(), (int)(winInfo.size() + 1));

                    for (auto c : clients)
                    {
                        std::string result = (c->id == winnerId)
                            ? "Stage ended. You are the winner! (Winner: Client " + std::to_string(winnerId) + ")"
                            : "Stage ended. You lost. (Winner: Client " + std::to_string(winnerId) + ")";
                        sendMessage(c->sock, 0, (int)ServerMessage::Type::MSG_INFO, result.c_str(), (int)(result.size() + 1));
                    }
                    std::cout << "Game round ended. Winner is Client " << winnerId << ".\n";
                    gameState = WAITING;
                }
            }
            break;

        default:
            std::cout << "Unknown message type from Client " << client->id << "\n";
            break;
        }
    }

    {
        std::lock_guard<std::recursive_mutex> lock(clientsMutex);
        auto it = std::find_if(clients.begin(), clients.end(), [client](Client* c) { return c->id == client->id; });

        if (it != clients.end())
        {
            bool wasRoomOwner = (client->id == roomOwnerId);
            clients.erase(it);
            std::cout << "Removed Client " << client->id << ".\n";
            const char* discMsg = "has left the room.";
            broadcastMessage(client->id, (int)ServerMessage::Type::MSG_DISCONNECT, &client->id, sizeof(client->id));

            if (clients.empty())
            {
                roomOwnerId = -1;
            }
            else if (wasRoomOwner)
            {
                roomOwnerId = clients.front()->id;
                std::string newOwnerMsg = "Room owner left. New owner is Client " + std::to_string(roomOwnerId) + ".";
                broadcastMessage(0, (int)ServerMessage::Type::MSG_INFO, newOwnerMsg.c_str(), (int)(newOwnerMsg.size() + 1));
                broadcastMessage(0, (int)ServerMessage::Type::MSG_NEW_OWNER, &roomOwnerId, sizeof(roomOwnerId));
                std::cout << "Room owner changed to Client " << roomOwnerId << ".\n";
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
        std::cerr << "WSAStartup failed.\n";
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
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

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

    listen(serverSock, SOMAXCONN);
    std::cout << "Server is running. Waiting for clients...\n";

    while (true)
    {
        sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        SOCKET clientSock = accept(serverSock, (sockaddr*)&clientAddr, &clientAddrSize);

        if (clientSock == INVALID_SOCKET)
        {
            std::cerr << "Accept failed.\n";
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
            sendMessage(newClient->sock, 0, (int)ServerMessage::Type::MSG_NEW_OWNER, &roomOwnerId, sizeof(roomOwnerId));

            // 🔥 접속 중 클라이언트 리스트 전송
            std::vector<int> idList;
            idList.push_back((int)clients.size());
            for (auto c : clients)
                idList.push_back(c->id);
            sendMessage(newClient->sock, 0, (int)ServerMessage::Type::MSG_CLIENT_LIST, idList.data(), (int)(idList.size() * sizeof(int)));

            if (roomOwnerId == -1)
            {
                roomOwnerId = newClient->id;
                sendMessage(newClient->sock, 0, (int)ServerMessage::Type::MSG_NEW_OWNER, &roomOwnerId, sizeof(roomOwnerId));
                const char* ownerMsg = "You are the room owner.";
                sendMessage(newClient->sock, newClient->id, (int)ServerMessage::Type::MSG_INFO, ownerMsg, (int)strlen(ownerMsg) + 1);
            }

            std::cout << "Client " << newClient->id << " connected.\n";

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
