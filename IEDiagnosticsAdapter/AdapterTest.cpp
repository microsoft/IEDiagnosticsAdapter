//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "AdapterTest.h"
#include "WebSocketHandler.h"
#include <assert.h>
#include <cassert>

using namespace std;

AdapterTest::AdapterTest(WebSocketHandler *handler, TestMode testMode):
	m_WebSocketHandler(handler),
	m_testMode(testMode)
{
	if (m_testMode == TestMode::RECORD)
	{
		textout.open("../test.txt");
	}

	if (m_testMode == TestMode::TEST)
	{
		ConnectToTestIE();
	}
}

// test infrastructure
bool AdapterTest::isSendCommand(const string &s) {
	return (s.substr(0, 5) == "send:");
}

void AdapterTest::ConnectToTestIE() {
	if (m_testMode != TestMode::TEST)
	{
		return;
	}

	m_WebSocketHandler->PopulateIEInstances();
	ifstream in;
	in.open("../test.txt");
	char str[600000];

	while (in.getline(str, 600000, '\v') && in.good())
	{
		string s(str);
		assert((testCommands.empty() || s.substr(0, 5) == "send:" || s.substr(0, 5) == "test:") && "not a valid test file");
		testCommands.push_back(string(str));
	}

	assert(!testCommands.empty() && "not a valid test file");

	string url = testCommands.front();
	testCommands.erase(testCommands.begin());

	m_WebSocketHandler->ConnectToUrl(url);
}

void AdapterTest::SendTestMessagesToIE(HWND proxyHwnd) {
	while (testCommands.size() > 0 && isSendCommand(testCommands[0]))
	{
		string s = testCommands[0].substr(5);
		CString message(s.c_str());
		m_WebSocketHandler->SendMessageToInstance(proxyHwnd, message);
		testCommands.erase(testCommands.begin());
	}
}

void AdapterTest::ValidateMessageFromIE(string message, HWND proxyHwnd) {
	bool foundMsg = false;
	for (auto it = testCommands.begin(); it != testCommands.end(); ++it)
	{
		if (it->substr(5) == message && !isSendCommand(*it))
		{
			testCommands.erase(it);
			foundMsg = true;
			break;
		}
	}

	assert(foundMsg);
	this->SendTestMessagesToIE(proxyHwnd);

	if (testCommands.size() == 0) {
		cout << "test passed!!" << endl;
	}
}

void AdapterTest::handleRecord(const std::string & s) {
	if (m_testMode == TestMode::RECORD) {
		// if the length is > 60000 the code to read it won't work
		assert(s.length() < 600000);
		textout << s << '\v';
	}
}

void AdapterTest::closeRecord() {
	if (m_testMode == TestMode::RECORD)
	{
		textout.close();
	}
}
