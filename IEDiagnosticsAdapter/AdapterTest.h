//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "Proxy_h.h"

class WebSocketHandler;
enum TestMode { NORMAL, RECORD, TEST };

class AdapterTest
{
public:
	AdapterTest(WebSocketHandler* handler, TestMode testMode);
	void ValidateMessageFromIE(std::string message, HWND proxyHwnd);
	void SendTestMessagesToIE(HWND proxyHwnd);
	void handleRecord(const std::string & s);
	void closeRecord();

private:
	bool isSendCommand(const std::string &s);
	void ConnectToTestIE();

public:
	const TestMode m_testMode;

private:
	WebSocketHandler* m_WebSocketHandler;
	std::ofstream textout;
	std::vector<std::string> testCommands;
};