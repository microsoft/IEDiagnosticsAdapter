//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "IEDiagnosticsAdapter.h"
#include "Helpers.h"
#include "resource.h"
#include "Strsafe.h"
#include <Psapi.h>

IEDiagnosticsAdapter::IEDiagnosticsAdapter(_In_ LPCWSTR rootPath) :
    m_rootPath(rootPath),
    m_port(9222)
{
    // Create our message window used to handle window messages
    HWND createdWindow = this->Create(HWND_MESSAGE);
    ATLENSURE_THROW(createdWindow != NULL, E_FAIL);

    // Allow messages from the proxy
    ::ChangeWindowMessageFilterEx(m_hWnd, WM_COPYDATA, MSGFLT_ALLOW, 0);
    ::ChangeWindowMessageFilterEx(m_hWnd, Get_WM_SET_CONNECTION_HWND(), MSGFLT_ALLOW, 0);

    // Initialize the websocket server
    m_server.clear_access_channels(websocketpp::log::alevel::all);

    m_server.set_http_handler(std::bind(&IEDiagnosticsAdapter::OnHttp, this, std::placeholders::_1));
    m_server.set_validate_handler(std::bind(&IEDiagnosticsAdapter::OnValidate, this, std::placeholders::_1));
    m_server.set_message_handler(std::bind(&IEDiagnosticsAdapter::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
    m_server.set_close_handler(std::bind(&IEDiagnosticsAdapter::OnClose, this, std::placeholders::_1));

    stringstream port;
    port << m_port;

    m_server.init_asio();
    m_server.listen("127.0.0.1", port.str());
    m_server.start_accept();

    cout << "Proxy server listening on port " << port.str() << "..." << endl;

    // Begin polling for data (we poll so we can respond to window messages too)
    this->Poll();
}

IEDiagnosticsAdapter::~IEDiagnosticsAdapter()
{
    if (::IsWindow(m_hWnd))
    {
        this->DestroyWindow();
    }
}

// Window Messages
LRESULT IEDiagnosticsAdapter::OnPollWebSocket(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    this->Poll();
    return 0;
}

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
    if (m_proxyConnections.find(proxyHwnd) != m_proxyConnections.end())
    {
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

        // Forward the message to the websocket
        try
        {
            m_server.send(m_proxyConnections[proxyHwnd], utf8, websocketpp::frame::opcode::text);
        }
        catch (const std::exception & e)
        {
            std::cout << "Execption during send: " << e.what() << std::endl;
        }
        catch (websocketpp::lib::error_code e)
        {
            std::cout << "Error during send: " << e.message() << std::endl;
        }
        catch (...)
        {
            std::cout << "Unknown exception during send" << std::endl;
        }
    }

    return 0;
}

// Helper functions
void IEDiagnosticsAdapter::Poll()
{
    try
    {
        m_server.poll();
    }
    catch (const std::exception & e) 
    {
        std::cout << "Execption during poll: " << e.what() << std::endl;
    }
    catch (websocketpp::lib::error_code e) 
    {
        std::cout << "Error during poll: " << e.message() << std::endl;
    }
    catch (...) 
    {
        std::cout << "Unknown exception during poll" << std::endl;
    }

    ::PostMessage(m_hWnd, WM_POLL_WEBSOCKET, NULL, NULL);
}

HRESULT IEDiagnosticsAdapter::PopulateIEInstances()
{
    map<HWND, IEInstance> current;

    // Enumerate all the windows looking for instances of Internet Explorer
    Helpers::EnumWindowsHelper([&](HWND hwndTop) -> BOOL
    {
        Helpers::EnumChildWindowsHelper(hwndTop, [&](HWND hwnd) -> BOOL
        {
            if (Helpers::IsWindowClass(hwnd, L"Internet Explorer_Server"))
            {
                DWORD processId;
                ::GetWindowThreadProcessId(hwnd, &processId);

                CComPtr<IHTMLDocument2> spDocument;
                HRESULT hr = Helpers::GetDocumentFromHwnd(hwnd, spDocument);
                if (hr == S_OK)
                {
                    UUID guid;
                    if (m_instances.find(hwnd) != m_instances.end())
                    {
                        if (!m_instances[hwnd].isConnected)
                        {
                            guid = m_instances[hwnd].guid;
                        }
                        else
                        {
                            current[hwnd] = m_instances[hwnd];
                            return TRUE;
                        }
                    }
                    else
                    {
                        ::UuidCreate(&guid);
                    }

                    CComBSTR url;
                    hr = spDocument->get_URL(&url);
                    if (hr != S_OK)
                    {
                        url = L"unknown";
                    }

                    CComBSTR title;
                    hr = spDocument->get_title(&title);
                    if (hr != S_OK)
                    {
                        title = L"";
                    }

                    CString filePath;
                    CHandle handle(::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId));
                    if (handle)
                    {
                        DWORD bufferSize = MAX_PATH;
                        DWORD count = ::GetModuleFileNameEx(handle, nullptr, filePath.GetBuffer(bufferSize), bufferSize);
                        filePath.ReleaseBufferSetLength(count);
                    }

                    current[hwnd] = IEInstance(guid, processId, hwnd, url, title, filePath);
                }
            }

            return TRUE;
        });

        return TRUE;
    });

    m_instances.clear();
    m_instances = current;

    return S_OK;
}

HRESULT IEDiagnosticsAdapter::ConnectToInstance(_In_ IEInstance& instance)
{
    if (instance.isConnected)
    {
        return E_NOT_VALID_STATE;
    }

    CComPtr<IHTMLDocument2> spDocument;
    HRESULT hr = Helpers::GetDocumentFromHwnd(instance.hwnd, spDocument);
    if (hr == S_OK)
    {
        CString path(m_rootPath);
        path.Append(L"Proxy.dll");

        CComPtr<IOleWindow> spSite;
        hr = Helpers::StartDiagnosticsMode(spDocument, __uuidof(ProxySite), path, __uuidof(spSite), reinterpret_cast<void**>(&spSite.p));
        if (hr == S_OK)
        {
            HWND hwnd;
            hr = spSite->GetWindow(&hwnd);
            FAIL_IF_NOT_S_OK(hr);

            // Send our hwnd to the proxy so it can connect back
            BOOL succeeded = ::PostMessage(hwnd, Get_WM_SET_CONNECTION_HWND(), reinterpret_cast<WPARAM>(m_hWnd), NULL);
            ATLENSURE_RETURN_HR(succeeded, E_FAIL);

            // Inject script onto the browser thread
            hr = this->InjectScript(L"browser", L"Common.js", IDR_COMMON_SCRIPT, hwnd);
            hr = this->InjectScript(L"browser", L"browserMain.js", IDR_BROWSER_SCRIPT, hwnd);

            // Inject script  onto the debugger thread
            hr = this->InjectScript(L"debugger", L"Common.js", IDR_COMMON_SCRIPT, hwnd);
            hr = this->InjectScript(L"debugger", L"debuggerMain.js", IDR_DEBUGGER_SCRIPT, hwnd);

            // Connected
            instance.isConnected = true;
            instance.spSite = spSite;
            instance.connectionHwnd = hwnd;
        }
    }

    return hr;
}

HRESULT IEDiagnosticsAdapter::SendMessageToInstance(_In_ HWND& instanceHwnd, _In_ CString& message)
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

    ::SendMessage(instanceHwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(m_hWnd), reinterpret_cast<LPARAM>(&copyData));

    return hr;
}

HRESULT IEDiagnosticsAdapter::InjectScript(_In_ const LPCWSTR id, _In_ const LPCWSTR scriptName, _In_ const DWORD resourceId, _In_ HWND hwnd)
{
    // Load the script that we will inject to onto the remote side
    CString script;
    HRESULT hr = Helpers::ReadFileFromModule(MAKEINTRESOURCE(resourceId), script);
    FAIL_IF_NOT_S_OK(hr);

    CString command;
    command.Format(L"inject:%ls:%ls:%ls", id, scriptName, script);
    hr = this->SendMessageToInstance(hwnd, command);
    FAIL_IF_NOT_S_OK(hr);

    return hr;
}

// WebSocket Callbacks
void IEDiagnosticsAdapter::OnHttp(websocketpp::connection_hdl hdl)
{
    server::connection_ptr con = m_server.get_con_from_hdl(hdl);

    stringstream ss;
    if (con->get_resource() == "/")
    {
        // Load and return the html selection page
        CString inspect;
        HRESULT hr = Helpers::ReadFileFromModule(MAKEINTRESOURCE(IDR_INSPECTHTML), inspect);
        if (hr == S_OK)
        {
            CStringA page(inspect);
            ss << page;
        }
    }
    else if (con->get_resource() == "/json" || con->get_resource() == "/json/list")
    {
        // Enumerate the running IE instances
        this->PopulateIEInstances();

        // Return a json array describing the instances
        size_t index = 0;
        ss << "[";
        for (auto& it : m_instances)
        {
            CStringA url(it.second.url);
            url.Replace('\\', '/');
            url.Replace(" ", "%20");
            url.Replace("file://", "file:///");
            CStringA title(it.second.title);
            CStringA fileName(::PathFindFileNameW(it.second.filePath));
            CComBSTR guidBSTR(it.second.guid);
            CStringA guid(guidBSTR);
            guid = guid.Mid(1, guid.GetLength() - 2);

            ss << "{" << endl;
            ss << "   \"description\" : \"" << fileName.MakeLower() << "\"," << endl;
            ss << "   \"devtoolsFrontendUrl\" : \"\"," << endl;
            ss << "   \"id\" : \"" << guid << "\"," << endl;
            ss << "   \"title\" : \"" << title << "\"," << endl;
            ss << "   \"type\" : \"page\"," << endl;
            ss << "   \"url\" : \"" << url << "\"," << endl;
            ss << "   \"webSocketDebuggerUrl\" : \"ws://" << con->get_host() << ":" << con->get_port() << "/devtools/page/" << guid << "\"" << endl;
            ss << "}";

            if (index < m_instances.size() - 1)
            {
                ss << ", ";
            }
            index++;
        }
        ss << "]";
    }

    con->set_body(ss.str());
    con->set_status(websocketpp::http::status_code::ok);
}


bool IEDiagnosticsAdapter::OnValidate(websocketpp::connection_hdl hdl)
{
    server::connection_ptr con = m_server.get_con_from_hdl(hdl);

    string resource = con->get_resource();
    size_t idIndex = resource.find_last_of("/");
    if (idIndex == string::npos)
    {
        // No identifier
        return false;
    }

    size_t typeIndex = resource.find_last_of("/", idIndex - 1);
    if (typeIndex == string::npos)
    {
        // No connection type
        return false;
    }

    // Get the connection type
    string connectionType = resource.substr(typeIndex + 1, (idIndex - typeIndex - 1));

    if (connectionType == "page")
    {
        // Convert the id string into the guid it is requesting
        string guidString = string("{") + resource.substr(idIndex + 1).c_str() + string("}");
        CComBSTR guidBstr(guidString.c_str());
        UUID guid;
        HRESULT hr = ::CLSIDFromString(guidBstr, &guid);
        if (hr == S_OK)
        {
            // Find that in our existing IE instances
            for (auto& i : m_instances)
            {
                if (i.second.guid == guid && ::IsWindow(i.second.hwnd))
                {
                    // Found a matching HWND so try to connect
                    hr = this->ConnectToInstance(i.second);
                    if (hr == S_OK)
                    {
                        // Connection established so map it to the IE Instance
                        m_clientConnections[hdl] = i.second.connectionHwnd;
                        m_proxyConnections[i.second.connectionHwnd] = hdl;

                        cout << "Client connection accepted for: " << resource << " as: " << i.second.hwnd << endl;
                        return true;
                    }
                }
            }
        }
    }

    // Invalid resource or no matching HWND
    cout << "Connection rejected for: " << resource << endl;
    return false;
}

void IEDiagnosticsAdapter::OnMessage(websocketpp::connection_hdl hdl, server::message_ptr msg)
{
    if (m_clientConnections.find(hdl) != m_clientConnections.end())
    {
         // Message from WebKit client to IE
        CString message(msg->get_payload().c_str());
        this->SendMessageToInstance(m_clientConnections[hdl], message);
    }
}

void IEDiagnosticsAdapter::OnClose(websocketpp::connection_hdl hdl)
{
    // Remove the connection and reset the instance into a usable state
    HWND instanceHwnd;
    if (m_clientConnections.find(hdl) != m_clientConnections.end())
    {
        for (auto& i : m_instances)
        {
            if (i.second.connectionHwnd == m_clientConnections[hdl])
            {
                instanceHwnd = i.first;
                break;
            }
        }

        m_proxyConnections.erase(m_clientConnections[hdl]);
        m_clientConnections.erase(hdl);
    }

    m_instances.erase(instanceHwnd);
}