#pragma once

#include "Client.h"

int main()
{
	CClient client;
	client.Init();

	// ����� �Է� ó��: "start" ����� MSG_START, �� �ܴ� float ������ (MSG_FLOAT_DATA)
	while (true)
	{
		std::string input;
		std::getline(std::cin, input);
		if (input.empty())
			continue;

		if (input == "start")
		{
			client.SendMsg(0, MSG_START, nullptr, 0);
		}
		else
		{
			try
			{
				float value = std::stof(input);
				client.SendMsg(0, MSG_FLOAT_DATA, &value, sizeof(float));
			}
			catch (...)
			{
				std::cerr << "Invalid input. Enter 'start' or a float value." << std::endl;
			}
		}
	}

	
}
