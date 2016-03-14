//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "AdapterTest.h"
#include "WebSocketHandler.h"
#include <boost/filesystem.hpp>
#include <assert.h>
#include <cassert>

using namespace std;
using namespace boost::filesystem;

AdapterTest::AdapterTest(WebSocketHandler *handler, HWND adpaterhWnd, TestMode testMode) :
	m_WebSocketHandler(handler),
	m_testMode(testMode),
	m_testIETab(nullptr),
	m_onTestNumber(1),
	m_testTimeoutNumber(1),
	m_adpaterhWnd(adpaterhWnd)
{
	//constructor runs on a different thread then every other function in this class. Put code in init() instead of here
}

void AdapterTest::init() {
	if (m_testMode == TestMode::RECORD)
	{
		m_textout.open("../test.txt");
	}

	if (m_testMode == TestMode::TEST)
	{
		findTests("../tests/");
		this->runNextTest();
	}
}

void AdapterTest::findTests(const std::string& pathString)
{
	m_WebSocketHandler->PopulateIEInstances();
	path dir_path(pathString);
	if (!exists(dir_path)) return;

	directory_iterator end_itr; // default construction yields past-the-end
	for (directory_iterator itr(dir_path); itr != end_itr; ++itr)
	{
		string testPath = itr->path().string();
		m_testFiles.push_back(testPath);
	}
}

void AdapterTest::runNextTest()
{
	if (!m_testFiles.empty()) {
		if (m_testIETab != nullptr) {
			// In theory this might cause a race condition where the next test can start before this message is handled, but that never seems to happen so leaving it for now
			CString msg(L"{\"method\":\"Custom.testResetState\"}");
			m_WebSocketHandler->SendMessageToInstance(m_testIETab->connectionHwnd, msg);
		}
		this->runTest(m_testFiles.back());
		m_testFiles.pop_back();
	}
	else {
		cout << "All Tests Passed!" << endl;
	}
}

// If we get a repose from IE that does not exactly match one of the responses in the test, we use the fingerprint to try to see which test response it lines up with
// there are two types of messages, responses to requests and notifications
// a response to a request has and ID associated with it, and the fingerprint is that ID
// Notifications do not have an ID, so the fingerprint we use is the method name. This unfortunately is less precise than using a ID because multiple requests can have the same message name, but it is good enough for now
string AdapterTest::getFingerprint(const string &jsonMessage) {
	if (jsonMessage.substr(0, 6) == "{\"id\":") {
		return jsonMessage.substr(6, jsonMessage.find(',') - 6);
	}
	assert((jsonMessage.substr(0, 10) == "{\"method\":") && "JSON does not have an ID and is not a notification");
	return jsonMessage.substr(10, jsonMessage.find(',') - 10);
}

void AdapterTest::runTest(const std::string& testFile) {
	if (m_testMode != TestMode::TEST)
	{
		return;
	}

    std::ifstream in;
	in.open(testFile);
	char buffer[MAX_RECORDED_STRING_LENGTH];

	// first line in the file is the URL, it is not formated the same as the rest of the commands, so get it before we parse everything else
	// \n is not a valid character in a URL so we can use it as a delimiter here, it often appears in our JSON so we need to use the less common \v
	in.getline(buffer, MAX_RECORDED_STRING_LENGTH, '\n');
	assert(in.peek() == '\v' && "invalid test file");
	in.get(); // get the \v charater and discard it
	string url(buffer); // the URL that this test needs to run against.
	
	while (in.getline(buffer, MAX_RECORDED_STRING_LENGTH, '\v') && in.good())
	{
		string testCommand(buffer);
		assert(testCommand.back() == '\n' && "invalid test file");
		testCommand.pop_back(); // remove \n that we added to make the test files easy to read. If \v ever actually appears in a JSON message we can use the "\n\v" to delineate messages
		
		msgType type;
		if (testCommand.substr(0, 5) == "send:") {
			type = msgType::SEND; // send messages are requests from the chrome dev tools that we will send to IE
		}
		else if (testCommand.substr(0, 5) == "resp:") {
			type = msgType::RESPONSE; // response messages are responses we need to receive before sending out future send messages, or the remote code may become confused.
			// however, they are not the part that the test is focusing on, so we do not need to validate all of the parameters match exactly, just use them for timing
		}
		else if (testCommand.substr(0, 5) == "skip:") {
			type = msgType::SKIP; // Skip messages are messages we may or may not receive from IE, and can be safely ignored
		}
		else {
			type = msgType::VALIDATE; // Validate messages are the same as response messages, except we validate that all of the parameters exactly match what the test expects
			// These messages are the heart of what the test is trying to validate
		}
	
		testMsg msgStruct;
		msgStruct.fingerprint = this->getFingerprint(testCommand.substr(5));
		msgStruct.fullMsg = testCommand;
		msgStruct.command = testCommand.substr(5);
		msgStruct.type = type;

		m_testCommands.push_back(msgStruct);
	}

	assert(!m_testCommands.empty() && "not a valid test file");
	if (m_testIETab == nullptr) {
		// case 1: not yet connected to a tab, because this is the first test
		m_testIETab = m_WebSocketHandler->ConnectToUrl(url);
	}
	else if (m_testIETab->url != url) {
		// case 2: Connected to a tab which is on the wrong url, need to navigate the tab to the correct url
		// todo: handle this case
		assert(false);
	}
	// case 3: we are already connected to the correct tab, so no work is needed

	PostMessage(m_adpaterhWnd, WM_TEST_START, 1, 0);
	this->SendTestMessagesToIE(m_testIETab->connectionHwnd);

	cout << "Connected to " << url << " beginning test " << testFile << endl;
}
void AdapterTest::SendTestMessagesToIE(HWND proxyHwnd) {
	while (m_testCommands.size() > 0 &&
		(m_testCommands[0].type == msgType::SEND || m_testCommands[0].type == msgType::SKIP))
	{
		// SKIP messages are responses that we may not receive and that we don't need to wait on before sending the next request. If we are waiting on a SKIP message, discard it and proceed as if we had received it
		if (m_testCommands[0].type == msgType::SEND) {
			string s = m_testCommands[0].command;
			CString message(s.c_str());
			m_WebSocketHandler->SendMessageToInstance(proxyHwnd, message);
		}
		m_testCommands.erase(m_testCommands.begin());
	}
}

void AdapterTest::ValidateMessageFromIE(string message, HWND proxyHwnd) {
	if (message == "testTimeout") {
		if (m_testTimeoutNumber >= m_onTestNumber) {
			std::cout << "test timeout!" << endl;
			cout << "dumping the missing messages" << endl;
			for (auto it : m_testCommands) {
				cout << it.fullMsg << endl;
			}
		}
		m_testTimeoutNumber++;
		return;
	}

	bool foundMsg = false;
	for (auto it = m_testCommands.begin(); it != m_testCommands.end(); ++it)
	{
		if (it->command == message && it->type != msgType::SEND)
		{
			// even if the type is not verify, if we find a perfect match might as well use the additional information
			m_testCommands.erase(it);
			foundMsg = true;
			break;
		}
	}

	if (!foundMsg) {
		string fingerprint = this->getFingerprint(message);
		// if we could not find an exact match, see if we can find a message with the same fingerprint (ID or notification type)
		for (auto it = m_testCommands.begin(); it != m_testCommands.end(); ++it)
		{
			if (it->fingerprint == fingerprint)
			{
				if (it->type == msgType::RESPONSE || it->type == msgType::SKIP)
				{
					foundMsg = true;
					m_testCommands.erase(it);
					break;
				}
				else if (it->type == msgType::VALIDATE && it->command.substr(0, 6) == "{\"id\":") {
					// we matched the ID of a validate response, but the actual response differed from expected.
					// todo: it would be nicer if the test did not also time out after this error
					m_textout << "test failed due to mismatched reposnse " << endl << "response: " << message << endl << endl << "expected: " << it->command << endl;

					// often the mismatched strings are pretty massive, and it is easiest to debug by dumping them to a file
					//textout.open("../diff.txt");
					//textout << "test failed due to mismatched reposnse " << endl << "response: " << message << endl << endl << "expected: " << it->command << endl;
					//textout.close();
				}
			}
		}
	}

	this->SendTestMessagesToIE(proxyHwnd);

	if (m_testCommands.size() == 0) {
		m_onTestNumber++;
		cout << "test passed!!" << endl;
		this->runNextTest();
	}
}

void AdapterTest::handleRecord(const std::string & s) {
	if (m_testMode == TestMode::RECORD) {
		// if the length is > MAX_RECORDED_STRING_LENGTH the code to read it won't work
		if (s.length() > MAX_RECORDED_STRING_LENGTH) {
			assert(false && "Cannot record messages longer than MAX_RECORDED_STRING_LENGTH characters");
			throw "Cannot record messages longer than MAX_RECORDED_STRING_LENGTH characters";
		}
		m_textout << s << "\n\v";
	}
}

void AdapterTest::closeRecord() {
	if (m_testMode == TestMode::RECORD)
	{
		m_textout.close();
	}
}
