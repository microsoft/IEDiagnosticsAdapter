//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "WebSocketClientHost.h"
#include "Strsafe.h"

using namespace std::placeholders;

WebSocketClientHost::WebSocketClientHost() : 
    ScriptEngineHost(),
    m_uiThreadHwnd(0),
    m_serverHwnd(0)
{
    // Allow messages from the server
    ::ChangeWindowMessageFilterEx(m_hWnd, WM_COPYDATA, MSGFLT_ALLOW, 0);
    ::ChangeWindowMessageFilterEx(m_hWnd, Get_WM_SET_CONNECTION_HWND(), MSGFLT_ALLOW, 0);
}

HRESULT WebSocketClientHost::Initialize(_In_ HWND mainHwnd)
{
    m_uiThreadHwnd = mainHwnd;

    HRESULT hr = ScriptEngineHost::Initialize(m_hWnd);
    FAIL_IF_NOT_S_OK(hr);

    return hr;
}

// Window Messages
LRESULT WebSocketClientHost::OnSetConnectionHwnd(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    // Store the HWND used to connect back to the proxy
    m_serverHwnd = reinterpret_cast<HWND>(wParam);

    return 0;
}

LRESULT WebSocketClientHost::OnSetMessageHwnd(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    // Take ownership of the string
    CComBSTR idBstr;
    idBstr.Attach(reinterpret_cast<BSTR>(wParam));
    HWND hwnd = reinterpret_cast<HWND>(lParam);

    // Store the engine hwnd
    CString id(idBstr);
    id.MakeLower();
    m_engineHosts[id] = hwnd;

    // Fire any queued messages
    if (m_engineMessageQueue.find(id) != m_engineMessageQueue.end())
    {
        for (auto& i : m_engineMessageQueue[id])
        {
            MessageInfo* pInfoParam = i.release();
            BOOL succeeded = ::PostMessage(m_engineHosts[id], WM_MESSAGE_RECEIVE, reinterpret_cast<WPARAM>(pInfoParam), 0);
            if (!succeeded)
            {
                i.reset(pInfoParam);
            }
        }

        m_engineMessageQueue[id].clear();
    }

    return 0;
}

LRESULT WebSocketClientHost::OnMessageSend(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    // Take ownership of the string
    CComBSTR message;
    message.Attach(reinterpret_cast<BSTR>(wParam));

    // Send the message to the server
    CString messageData(message);
    this->SendMessageToWebKit(messageData);

    return 0;
}

LRESULT WebSocketClientHost::OnMessageReceive(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    // Take ownership of the data
    unique_ptr<MessageInfo> spInfo(reinterpret_cast<MessageInfo*>(wParam));

    // Forward the message to the correct thread
    this->SendMessageToScriptHost(std::move(spInfo), /*shouldCreateEngine*/false);

    return 0;
}

LRESULT WebSocketClientHost::OnCopyData(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
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

LRESULT WebSocketClientHost::OnMessageFromWebKit(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    CString message;

    // Scope for the copied data
    {
        // Take ownership of the copydata struct memory
        unique_ptr<COPYDATASTRUCT, void(*)(COPYDATASTRUCT*)> spParams(reinterpret_cast<PCOPYDATASTRUCT>(lParam), ::FreeCopyDataStructCopy);

        // Get the string message from the structure
        CopyDataPayload_StringMessage_Data* pMessage = reinterpret_cast<CopyDataPayload_StringMessage_Data*>(spParams->lpData);
        LPCWSTR lpString = reinterpret_cast<LPCWSTR>(reinterpret_cast<BYTE*>(pMessage) + pMessage->uMessageOffset);
        message = lpString;
    }

    // We send all messages to the debugger by default, so that it can handle code at a breakpoint
    CString id(L"debugger");
    CString scriptName(L"");
    bool isInjectionMessage = false;

    // Check if this is a script injection message
    if (message.GetLength() > 7)
    {
        if (message.Left(7).CompareNoCase(L"inject:") == 0)
        {
            isInjectionMessage = true;

            int idIndex = message.Find(L":", 7);
            if (idIndex > 7)
            {
                id = message.Mid(7, idIndex - 7);
                id.MakeLower();

                idIndex++; // Skip ':'

                int nameIndex = message.Find(L":", idIndex);
                if (nameIndex > idIndex)
                {
                    scriptName = message.Mid(idIndex, nameIndex - idIndex ); 
                    message = message.Mid(nameIndex + 1);
                }
            }
        }
    }

    // Send the message to the correct thread
    unique_ptr<MessageInfo> spInfo(new MessageInfo());
    spInfo->m_engineId = id;
    spInfo->m_scriptName = scriptName;
    spInfo->m_messageType = (isInjectionMessage ? MessageType::Inject : MessageType::Execute);
    spInfo->m_message = message;

    HRESULT hr = this->SendMessageToScriptHost(std::move(spInfo), isInjectionMessage);
    FAIL_IF_NOT_S_OK(hr);

    return 0;
}

// Helper functions
HRESULT WebSocketClientHost::SendMessageToScriptHost(_In_ unique_ptr<MessageInfo>&& spInfo, _In_ bool shouldCreateEngine)
{
    HRESULT hr = S_OK;

    CString id(spInfo->m_engineId);

    if (m_engineHosts.find(id) != m_engineHosts.end())
    {
        // Send the message to the specified engine thread
        
        MessageInfo* pInfoParam = spInfo.release();
        BOOL succeeded = ::PostMessage(m_engineHosts[id], WM_MESSAGE_RECEIVE, reinterpret_cast<WPARAM>(pInfoParam), 0);
        if (!succeeded)
        {
            spInfo.reset(pInfoParam);
            hr = E_FAIL;
        }
    }
    else
    {
        // Store this script ready for executing when the engine starts
        m_engineMessageQueue[id].push_back(std::move(spInfo));

        if (shouldCreateEngine)
        {
            // Create the new host for this id
            CComBSTR idBstr(id);
            BSTR param = idBstr.Detach();
            BOOL succeeded = ::PostMessage(m_uiThreadHwnd, WM_CREATE_ENGINE, reinterpret_cast<WPARAM>(param), 0);
            if (!succeeded)
            {
                idBstr.Attach(param);
                hr = E_FAIL;
            }
        }
    }

    return hr;
}

HRESULT WebSocketClientHost::SendMessageToWebKit(_In_ CString& message)
{
    const size_t ucbParamsSize = sizeof(CopyDataPayload_StringMessage_Data);
    const size_t ucbStringSize = sizeof(WCHAR) * (::wcslen(message) + 1);
    const size_t ucbBufferSize = ucbParamsSize + ucbStringSize;
    std::unique_ptr<BYTE> pBuffer;
    try
    {
        pBuffer.reset(new BYTE[ucbBufferSize]);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    COPYDATASTRUCT copyData;
    copyData.dwData = CopyDataPayload_ProcSignature::StringMessage_Signature;
    copyData.cbData = static_cast<DWORD>(ucbBufferSize);
    copyData.lpData = pBuffer.get();

    CopyDataPayload_StringMessage_Data* pData = reinterpret_cast<CopyDataPayload_StringMessage_Data*>(pBuffer.get());
    pData->uMessageOffset = static_cast<UINT>(ucbParamsSize);

    HRESULT hr = ::StringCbCopyEx(reinterpret_cast<LPWSTR>(pBuffer.get() + pData->uMessageOffset), ucbStringSize, message, NULL, NULL, STRSAFE_IGNORE_NULLS);
    FAIL_IF_NOT_S_OK(hr);

    ::SendMessage(m_serverHwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(m_hWnd), reinterpret_cast<LPARAM>(&copyData));

    return hr;
}
