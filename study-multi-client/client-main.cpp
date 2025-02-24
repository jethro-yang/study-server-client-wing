#pragma once

#include "Client.h"



// --- Helper Functions ---

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
	MessageHeader header;
	header.senderId = senderId;
	header.msgType = msgType;
	header.bodyLen = bodyLen;

	if (!sendAll(sock, reinterpret_cast<char*>(&header), sizeof(header)))
		return false;

	if (bodyLen > 0 && body != nullptr)
	{
		if (!sendAll(sock, reinterpret_cast<const char*>(body), bodyLen))
			return false;
	}
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
	{
		if (!recvAll(sock, bodyBuffer.data(), header.bodyLen))
			return false;
	}
	return true;
}

// --- Ŭ���̾�Ʈ ������ �Լ� ---

// �����κ��� �޽����� �����Ͽ� ó��
void receiveThread(SOCKET sock)
{
	while (true)
	{
		MessageHeader header;
		std::vector<char> bodyBuffer;
		if (!receiveMessage(sock, header, bodyBuffer))
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
				// body�� ���ڿ��� ���� (�� ���� ���� ����)
				std::cout << bodyBuffer.data();
			}
		}
		std::cout << std::endl;
	}
}

// 1�ʸ��� ��Ʈ��Ʈ �޽��� ����
void heartbeatThread(SOCKET sock)
{
	while (true)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		sendMessage(sock, 0, MSG_HEARTBEAT, nullptr, 0);
	}
}

int main()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		std::cerr << "WSAStartup failed." << std::endl;
		return 1;
	}

	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
	{
		std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
		WSACleanup();
		return 1;
	}

	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);

	if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) <= 0)
	{
		std::cerr << "Invalid address." << std::endl;
		closesocket(sock);
		WSACleanup();
		return 1;
	}

	if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		std::cerr << "Connection failed: " << WSAGetLastError() << std::endl;
		closesocket(sock);
		WSACleanup();
		return 1;
	}

	std::cout << "Connected to server." << std::endl;
	std::thread recvThread(receiveThread, sock);
	std::thread hbThread(heartbeatThread, sock);

	// ����� �Է� ó��: "start" ����� MSG_START, �� �ܴ� float ������ (MSG_FLOAT_DATA)
	while (true)
	{
		std::string input;
		std::getline(std::cin, input);
		if (input.empty())
			continue;

		if (input == "start")
		{
			sendMessage(sock, 0, MSG_START, nullptr, 0);
		}
		else
		{
			try
			{
				float value = std::stof(input);
				sendMessage(sock, 0, MSG_FLOAT_DATA, &value, sizeof(float));
			}
			catch (...)
			{
				std::cerr << "Invalid input. Enter 'start' or a float value." << std::endl;
			}
		}
	}

	// ���� �������Լ��� ���� ����� �� ����
	// ���� �����尡 ��ϻ��°� �ȴ�.
	recvThread.join();
	hbThread.join();
	closesocket(sock);
	WSACleanup();
	return 0;
}
