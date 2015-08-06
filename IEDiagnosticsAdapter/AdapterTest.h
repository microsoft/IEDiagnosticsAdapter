//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "Proxy_h.h"

#define MAX_RECORDED_STRING_LENGTH 60000

class WebSocketHandler;
struct IEInstance;

enum TestMode { NORMAL, RECORD, TEST };
enum msgType  { SEND, RESPONSE, VALIDATE, SKIP };

struct testMsg
{
	msgType type;
	string fingerprint; // either the ID associated with the message, or the method name if it is a notification
	string command;
	string fullMsg;
};

using namespace ATL;
using namespace std;

class AdapterTest
{
public:
	AdapterTest(WebSocketHandler* handler, HWND adpaterhWnd, TestMode testMode);
	void ValidateMessageFromIE(std::string message, HWND proxyHwnd);
	void SendTestMessagesToIE(HWND proxyHwnd);
	void handleRecord(const std::string & s);
	void closeRecord();
	void init();

private:
	void runNextTest();
	std::string getFingerprint(const std::string &s);
	void runTest(const std::string& testFile);
	void findTests(const std::string& path);

public:
	const TestMode m_testMode;

private:
	UINT m_onTestNumber;
	UINT m_testTimeoutNumber;
	HWND m_adpaterhWnd;
	WebSocketHandler* m_WebSocketHandler;
	IEInstance* m_testIETab;
	std::vector<std::string> m_testFiles;
	std::ofstream m_textout;
	std::vector<testMsg> m_testCommands;
};