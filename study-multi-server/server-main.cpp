// 주요 헤더 생략 없이 포함
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <string>
#include <algorithm>
#include <unordered_map>
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
    bool isReady = false;
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

void notifyReadyStatus()
{
    int readyCount = 0;
    for (auto c : clients)
    {
        if (c->id != roomOwnerId && c->isReady)
            readyCount++;
    }

    int expected = (int)clients.size() - 1;
    if (expected > 0 && readyCount == expected)
    {
        // 모든 비방장 유저가 레디
        std::string msg = "All players are ready. Host can start the game.";
        for (auto c : clients)
        {
            if (c->id == roomOwnerId)
            {
                sendMessage(c->sock, 0, (int)ServerMessage::Type::MSG_INFO, msg.c_str(), (int)(msg.size() + 1));
                break;
            }
        }
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
                const char* startMsg = "Game started!";
                broadcastMessage(client->id, (int)ServerMessage::Type::MSG_START_ACK, startMsg, (int)strlen(startMsg) + 1);
                std::cout << "Game started by host " << client->id << "\n";
            }
            break;

        case (int)ClientMessage::Type::MSG_PICK_CHARACTER:
            if (header.bodyLen == sizeof(int))
            {
                broadcastMessage(client->id, (int)ServerMessage::Type::MSG_PICK_CHARACTER, bodyBuffer.data(), sizeof(int));
            }
            break;

        case (int)ClientMessage::Type::MSG_PICK_ITEM:
            if (header.bodyLen == sizeof(ItemSelectInfo))
            {
                broadcastMessage(client->id, (int)ServerMessage::Type::MSG_PICK_ITEM, bodyBuffer.data(), sizeof(ItemSelectInfo));
            }
            break;

        case (int)ClientMessage::Type::MSG_READY:
            client->isReady = true;
            broadcastMessage(client->id, (int)ServerMessage::Type::MSG_READY, nullptr, 0);
            notifyReadyStatus();
            break;

        case (int)ClientMessage::Type::MSG_UNREADY:
            client->isReady = false;
            broadcastMessage(client->id, (int)ServerMessage::Type::MSG_UNREADY, nullptr, 0);
            break;

        case (int)ClientMessage::Type::MSG_MOVE_UP:
            broadcastMessage(client->id, (int)ServerMessage::Type::MSG_MOVE_UP, nullptr, 0);
            break;

        case (int)ClientMessage::Type::MSG_MOVE_DOWN:
            broadcastMessage(client->id, (int)ServerMessage::Type::MSG_MOVE_DOWN, nullptr, 0);
            break;

        default:
            break;
        }
    }

    // 클라이언트 종료 처리
    {
        std::lock_guard<std::recursive_mutex> lock(clientsMutex);
        auto it = std::find_if(clients.begin(), clients.end(), [client](Client* c) { return c->id == client->id; });

        if (it != clients.end())
        {
            bool wasOwner = (client->id == roomOwnerId);
            clients.erase(it);
            std::cout << "Removed Client " << client->id << "\n";

            // 누가 나갔는지 브로드캐스트
            broadcastMessage(client->id, (int)ServerMessage::Type::MSG_DISCONNECT, &client->id, sizeof(client->id));

            if (clients.empty())
            {
                roomOwnerId = -1;
            }
            else if (wasOwner)
            {
                roomOwnerId = clients.front()->id;
                broadcastMessage(0, (int)ServerMessage::Type::MSG_NEW_OWNER, &roomOwnerId, sizeof(roomOwnerId));
            }
        }
    }

    closesocket(client->sock);
    delete client;
}

int main()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSock, SOMAXCONN);
    std::cout << "Server running on port " << PORT << "\n";

    while (true)
    {
        sockaddr_in clientAddr{};
        int addrSize = sizeof(clientAddr);
        SOCKET clientSock = accept(serverSock, (sockaddr*)&clientAddr, &addrSize);

        Client* newClient = new Client;
        newClient->sock = clientSock;
        newClient->id = nextClientId++;

        {
            std::lock_guard<std::recursive_mutex> lock(clientsMutex);
            clients.push_back(newClient);

            sendMessage(newClient->sock, newClient->id, (int)ServerMessage::Type::MSG_CONNECTED, &newClient->id, sizeof(newClient->id));
            sendMessage(newClient->sock, 0, (int)ServerMessage::Type::MSG_NEW_OWNER, &roomOwnerId, sizeof(roomOwnerId));

            std::vector<int> idList;
            idList.push_back((int)clients.size());
            for (auto c : clients)
                idList.push_back(c->id);
            sendMessage(newClient->sock, 0, (int)ServerMessage::Type::MSG_CLIENT_LIST, idList.data(), (int)(idList.size() * sizeof(int)));

            if (roomOwnerId == -1)
            {
                roomOwnerId = newClient->id;
                sendMessage(newClient->sock, 0, (int)ServerMessage::Type::MSG_NEW_OWNER, &roomOwnerId, sizeof(roomOwnerId));
            }
        }

        newClient->thread = std::thread(clientThread, newClient);
        newClient->thread.detach();
    }

    closesocket(serverSock);
    WSACleanup();
    return 0;
}
