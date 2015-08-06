//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "IEDiagnosticsAdapter.h"
#include "WebSocketHandler.h"
#include "Helpers.h"
#include "resource.h"
#include "Strsafe.h"
#include <Psapi.h>

const CStringA IEDiagnosticsAdapter::s_Protocol_Version = CStringA("1.1");

IEDiagnosticsAdapter::IEDiagnosticsAdapter(_In_ LPCWSTR rootPath)
{
    // Create our message window used to handle window messages
    HWND createdWindow = this->Create(HWND_MESSAGE);
    ATLENSURE_THROW(createdWindow != NULL, E_FAIL);

    // Allow messages from the proxy
    ::ChangeWindowMessageFilterEx(m_hWnd, WM_COPYDATA, MSGFLT_ALLOW, 0);
    ::ChangeWindowMessageFilterEx(m_hWnd, Get_WM_SET_CONNECTION_HWND(), MSGFLT_ALLOW, 0);

    // Create websocket thread
    m_webSocketHander = ::make_shared<WebSocketHandler>(rootPath, m_hWnd);
    boost::thread serverThread(&WebSocketHandler::RunServer, m_webSocketHander);
}

IEDiagnosticsAdapter::~IEDiagnosticsAdapter()
{
    if (::IsWindow(m_hWnd))
    {
        this->DestroyWindow();
    }
}

// Window Messages
LRESULT IEDiagnosticsAdapter::OnCopyData(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    PCOPYDATASTRUCT pCopyDataStruct = reinterpret_cast<PCOPYDATASTRUCT>(lParam);

    // Copy the data so that we can post message to ourselves and unblock the SendMessage caller
    unique_ptr<COPYDATASTRUCT, void(*)(COPYDATASTRUCT*)> spParams(::MakeCopyDataStructCopy(pCopyDataStruct), ::FreeCopyDataStructCopy);

    PCOPYDATASTRUCT pParams = spParams.release();
    BOOL succeeded = this->PostMessageW(WM_PROCESSCOPYDATA, wParam, reinterpret_cast<LPARAM>(pParams));
    if (!succeeded)
    {
        // Take ownership if the post message fails so that we can correctly clean up the memory
        HRESULT hr = ::AtlHresultFromLastError();
        spParams.reset(pParams);
        FAIL_IF_NOT_S_OK(hr);
    }

    return 0;
}

LRESULT IEDiagnosticsAdapter::OnMessageFromIE(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    CString message;
    // Scope for the copied data
    {
        // Take ownership of the copydata struct memory
        unique_ptr<COPYDATASTRUCT, void(*)(COPYDATASTRUCT*)> spParams(reinterpret_cast<PCOPYDATASTRUCT>(lParam), ::FreeCopyDataStructCopy);

        // Get the string message from the structure
        CopyDataPayload_StringMessage_Data* pMessage = reinterpret_cast<CopyDataPayload_StringMessage_Data*>(spParams->lpData);
        LPCWSTR lpString = reinterpret_cast<LPCWSTR>(reinterpret_cast<BYTE*>(pMessage)+pMessage->uMessageOffset);
        message = lpString;
    }

    HWND proxyHwnd = reinterpret_cast<HWND>(wParam);

    string utf8;
    int length = message.GetLength();
    LPWSTR buffer = message.GetBuffer();

    // Convert the message into valid UTF-8 text
    int utf8Length = ::WideCharToMultiByte(CP_UTF8, 0, buffer, length, nullptr, 0, nullptr, nullptr);
    if (utf8Length == 0)
    {
        message.ReleaseBuffer();
        ATLENSURE_RETURN_HR(false, ::GetLastError());
    }

    utf8.resize(utf8Length);
    utf8Length = ::WideCharToMultiByte(CP_UTF8, 0, buffer, length, &utf8[0], static_cast<int>(utf8.length()), nullptr, nullptr);
    message.ReleaseBuffer();
    ATLENSURE_RETURN_HR(utf8Length > 0, ::GetLastError());



    // Now that we have parsed out the arguments, let the websocketHandler handle it
    m_webSocketHander->OnMessageFromIE(utf8, proxyHwnd);
    return 0;
}

// These functions handle test timeing out. This code cannot live in AdapterTest.cpp because that thread is blocked waiting on the websocket.
void CALLBACK TimerProc(HWND hWnd, UINT nMsg, UINT_PTR nIDEvent, DWORD dwTime)
{
	::KillTimer(hWnd, nIDEvent);
	PostMessage(hWnd, WM_TEST_TIMEOUT, nIDEvent, 0);
}

LRESULT IEDiagnosticsAdapter::OnTestTimeout(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
	m_webSocketHander->OnMessageFromIE(/*message=*/"testTimeout", nullptr);
	return 0;
}

LRESULT IEDiagnosticsAdapter::OnTestStart(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
	SetTimer(1/*testNumber*/,/*timeout=*/ 1000, TimerProc);
	return 0;
}