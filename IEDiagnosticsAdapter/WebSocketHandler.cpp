//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "WebSocketHandler.h"
#include "IEDiagnosticsAdapter.h"
#include "Helpers.h"
#include "resource.h"
#include "Strsafe.h"
#include "AdapterTest.h"
#include <VersionHelpers.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <Psapi.h>
#include <Wininet.h>

WebSocketHandler::WebSocketHandler(_In_ LPCWSTR rootPath, _In_ HWND hWnd) :
m_rootPath(rootPath),
m_hWnd(hWnd),
m_port(9222),
m_adapterTest(this, TestMode::NORMAL)
{
    // Initialize the websocket server
    m_server.clear_access_channels(websocketpp::log::alevel::all);

    m_server.set_http_handler(std::bind(&WebSocketHandler::OnHttp, this, std::placeholders::_1));
    m_server.set_validate_handler(std::bind(&WebSocketHandler::OnValidate, this, std::placeholders::_1));
    m_server.set_message_handler(std::bind(&WebSocketHandler::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
    m_server.set_close_handler(std::bind(&WebSocketHandler::OnClose, this, std::placeholders::_1));

    std::stringstream port;
    port << m_port;

    m_server.init_asio();
    m_server.listen("0.0.0.0", port.str());
    m_server.start_accept();

	CString AdaptorLogging_EnvironmentVariable;
	DWORD ret = AdaptorLogging_EnvironmentVariable.GetEnvironmentVariable(L"AdapterLogging");
	if (ret > 0 && AdaptorLogging_EnvironmentVariable == L"1") {
		std::cout << "Logging enabled" << endl;
		m_AdaptorLogging_EnvironmentVariable = "1";
	} 
	else {
		m_AdaptorLogging_EnvironmentVariable = "";
	}

    std::cout << "Proxy server listening on port " << port.str() << "..." << endl;
}

// WebSocket Callbacks
void WebSocketHandler::OnHttp(websocketpp::connection_hdl hdl)
{
    server::connection_ptr con = m_server.get_con_from_hdl(hdl);

    std::stringstream ss;

    std::string requestedResource = con->get_resource();

    if (requestedResource == "/")
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
    else if (requestedResource == "/json" || requestedResource == "/json/list")
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
            CStringA title = Helpers::EscapeJsonString(it.second.title);
            CStringA fileName = Helpers::EscapeJsonString(::PathFindFileNameW(it.second.filePath));

            CComBSTR guidBSTR(it.second.guid);
            CStringA guid(guidBSTR);
            guid = guid.Mid(1, guid.GetLength() - 2);

            std::string strWebSocketDebuggerUrl("ws://");
            strWebSocketDebuggerUrl += con->get_host();
            strWebSocketDebuggerUrl += ":";
            strWebSocketDebuggerUrl += std::to_string(con->get_port());
            strWebSocketDebuggerUrl += "/devtools/page/";
            strWebSocketDebuggerUrl += guid;
            CStringA webSocketDebuggerUrl = Helpers::EscapeJsonString(CString(strWebSocketDebuggerUrl.c_str()));

            std::string strDevtoolsFrontendUrl("http://");
            strDevtoolsFrontendUrl += con->get_host();
            strDevtoolsFrontendUrl += ":";
            strDevtoolsFrontendUrl += "9223"; // The remote port for the hidden Chrome instance that serves the tools
            strDevtoolsFrontendUrl += "/devtools/inspector.html?";
            strDevtoolsFrontendUrl += strWebSocketDebuggerUrl.substr(5);
            CStringA devtoolsFrontendUrl = Helpers::EscapeJsonString(CString(strDevtoolsFrontendUrl.c_str()));

            ss << "{" << endl;
            ss << "   \"description\" : \"" << fileName.MakeLower() << "\"," << endl;
            ss << "   \"devtoolsFrontendUrl\" : \"" << devtoolsFrontendUrl << "\"," << endl;
            ss << "   \"id\" : \"" << guid << "\"," << endl;
            ss << "   \"title\" : \"" << title << "\"," << endl;
            ss << "   \"type\" : \"page\"," << endl;
            ss << "   \"url\" : \"" << url << "\"," << endl;
            ss << "   \"webSocketDebuggerUrl\" : \"" << webSocketDebuggerUrl << "\"" << endl;
            ss << "}";

            if (index < m_instances.size() - 1)
            {
                ss << ", ";
            }
            index++;
        }
        ss << "]";
    }
    else if (requestedResource == "/json/version")
    {
        // To do: This will need to change to support Edge 
        CStringA ieVersion = Helpers::GetFileVersion(L"C:\\Windows\\System32\\mshtml.dll");
        CStringA browser = "Internet Explorer " + ieVersion;
        browser = Helpers::EscapeJsonString(CString(browser));


        DWORD dwUASize = 0;
        UrlMkGetSessionOption(URLMON_OPTION_USERAGENT, nullptr, 0, &dwUASize, 0);

        // Allocate string for current user agent:
        string strUserAgent(dwUASize, '\0');

        // Get current user agent:
        PSTR pszUserAgent = const_cast<PSTR>(strUserAgent.c_str()); 
        DWORD dwUASizeOut = 0;
        UrlMkGetSessionOption(URLMON_OPTION_USERAGENT, pszUserAgent, dwUASize, &dwUASizeOut, 0); // Don't check return value - this api always returns an error

        CStringA userAgent =  Helpers::EscapeJsonString(CString(pszUserAgent));

        ss << "{" << endl;
        ss << "   \"Browser\" : \"" << browser << "\"" << endl;
        ss << "   \"Protocol-Version\" : \"" << IEDiagnosticsAdapter::s_Protocol_Version << "\"" << endl;
        ss << "   \"User-Agent\" : \"" << userAgent << "\"" << endl;
        ss << "   \"WebKit-Version\" : \"" << "0" << "\"" << endl;
        ss << "}";
    }

    con->set_body(ss.str());
    con->set_status(websocketpp::http::status_code::ok);
}

bool WebSocketHandler::OnValidate(websocketpp::connection_hdl hdl)
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
    boost::algorithm::to_lower(connectionType);

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

						if (m_adapterTest.m_testMode == TestMode::RECORD) {
							// convert i.second.url from a CstringW to a normal std::string so we can record it 
							const std::wstring wideUrlString(i.second.url.GetString());
							const std::string urlString(wideUrlString.begin(), wideUrlString.end());
							m_adapterTest.handleRecord(urlString);
						}

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

void WebSocketHandler::OnMessage(websocketpp::connection_hdl hdl, server::message_ptr msg)
{
	if (m_adapterTest.m_testMode == TestMode::TEST)
	{
		return;
	}

    if (m_clientConnections.find(hdl) != m_clientConnections.end())
    {
		if(m_AdaptorLogging_EnvironmentVariable == "1")
		{
			std::cout << msg->get_payload().c_str() << "\n";
		}
		
		m_adapterTest.handleRecord("send:" + string(msg->get_payload().c_str()));

        // Message from WebKit client to IE
        CString message(msg->get_payload().c_str());
        this->SendMessageToInstance(m_clientConnections[hdl], message);
    }
}

void WebSocketHandler::OnClose(websocketpp::connection_hdl hdl)
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

		CString msg(L"{\"method\":\"Custom.toolsDisconnected\"}");
		this->SendMessageToInstance(m_clientConnections[hdl], msg);

		m_proxyConnections.erase(m_clientConnections[hdl]);
		m_clientConnections.erase(hdl);
		m_adapterTest.closeRecord();
	}
}

// Helper functions
HRESULT WebSocketHandler::PopulateIEInstances()
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
                        BOOL temp = FALSE;
                        bool is64bitOS = (::IsWow64Process(::GetCurrentProcess(), &temp) && temp);
                        temp = FALSE;
                        bool is64bitTab = is64bitOS && !(::IsWow64Process(handle, &temp) && temp);

                        DWORD bufferSize = MAX_PATH;
                        DWORD count = ::GetModuleFileNameEx(handle, nullptr, filePath.GetBuffer(bufferSize), bufferSize);
                        filePath.ReleaseBufferSetLength(count);

                        current[hwnd] = IEInstance(guid, processId, hwnd, url, title, filePath, is64bitTab);
                    }
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

void WebSocketHandler::RunServer() {
    // Initialize com on the server thread
    HRESULT hrCoInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    shared_ptr<HRESULT> spCoInit(&hrCoInit, [](const HRESULT* hrCom) -> void { if (SUCCEEDED(*hrCom)) { ::CoUninitialize(); } });

    // run the server
    while (true) {
        try
        {
            m_server.run();
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
    }
};

void WebSocketHandler::OnMessageFromIE(string message, HWND proxyHwnd)
{
	if (m_AdaptorLogging_EnvironmentVariable == "1") {
		std::cout << message << "\n";
	}

    // post this message to our IO queue so the server thread will pick it up and handle it synchronously
    this->m_server.get_io_service().post(boost::bind(&WebSocketHandler::OnMessageFromIEHandler, this, message, proxyHwnd));
}

void WebSocketHandler::OnMessageFromIEHandler(string message, HWND proxyHwnd) {
	m_adapterTest.handleRecord("test:" + message);
	if (m_adapterTest.m_testMode == TestMode::TEST)
	{
		m_adapterTest.ValidateMessageFromIE(message, proxyHwnd);
		return;
	}

    if (m_proxyConnections.find(proxyHwnd) != m_proxyConnections.end())
    {
        // Forward the message to the websocket
        try
        {
            m_server.send(m_proxyConnections[proxyHwnd], message, websocketpp::frame::opcode::text);
        }
        catch (const std::exception & e)
        {
            std::cout << "Exception during send: " << e.what() << std::endl;
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
}

// Function to let tests automaticly connect to the correct tab
void WebSocketHandler::ConnectToUrl(const string &url) {
	for (auto& i : m_instances)
	{
		if (i.second.url == CString(url.c_str()) && ::IsWindow(i.second.hwnd))
		{
			HRESULT hr = this->ConnectToInstance(i.second);
			if (hr == S_OK)
			{
				cout << "Connected to " << url << " begining test" << endl;
				m_adapterTest.SendTestMessagesToIE(i.second.connectionHwnd);
				return;
			}
		}
	}
	
	assert(false && "Could not find IE instance to attach to");
}


HRESULT WebSocketHandler::ConnectToInstance(_In_ IEInstance& instance)
{
    if (instance.isConnected)
    {
		return S_OK;
    }

    CComPtr<IHTMLDocument2> spDocument;
    HRESULT hr = Helpers::GetDocumentFromHwnd(instance.hwnd, spDocument);
    if (hr == S_OK)
    {
        CString path(m_rootPath);

        if (instance.is64BitTab)
        {
            #ifdef NDEBUG
                std::cout << "DEBUG MESSAGE: Attempting to attach to 64 bit tab" << std::endl;
            #endif
            path.Append(L"Proxy64.dll");
        }
        else
        {
            path.Append(L"Proxy.dll");
        }

        CComPtr<IOleWindow> spSite;
        hr = Helpers::StartDiagnosticsMode(spDocument, __uuidof(ProxySite), path, __uuidof(spSite), reinterpret_cast<void**>(&spSite.p));
        if (hr == E_ACCESSDENIED && instance.is64BitTab && ::IsWindows8Point1OrGreater())
        {
            std::cout << "ERROR: Access denied while attempting to connect to a 64 bit tab. The most common solution to this problem is to open an Administrator command prompt, navigate to the folder containing this adapter, and type \"icacls proxy64.dll /grant \"ALL APPLICATION PACKAGES\":(RX)\"" << std::endl;
        }
        else if (hr == ::HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND) && instance.is64BitTab) {
            std::cout << "ERROR: Module could not be found. Ensure Proxy64.dll is in the same folder as IEDiagnosticsAdaptor.exe" << std::endl;
        }
        else if (hr == ::HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND) && !instance.is64BitTab) {
            std::cout << "ERROR: Module could not be found. Ensure Proxy.dll is in the same folder as IEDiagnosticsAdaptor.exe" << std::endl;
        }
        else if (hr == S_OK)
        {
            HWND hwnd;
            hr = spSite->GetWindow(&hwnd);
            FAIL_IF_NOT_S_OK(hr);

            // Send our hwnd to the proxy so it can connect back
            BOOL succeeded = ::PostMessage(hwnd, Get_WM_SET_CONNECTION_HWND(), reinterpret_cast<WPARAM>(m_hWnd), NULL);
            ATLENSURE_RETURN_HR(succeeded, E_FAIL);

            // Inject script onto the browser thread
            hr = this->InjectScript(L"browser", L"Assert.js", IDR_ASSERT_SCRIPT, hwnd);
            hr = this->InjectScript(L"browser", L"Common.js", IDR_COMMON_SCRIPT, hwnd);
            hr = this->InjectScript(L"browser", L"browserMain.js", IDR_BROWSER_SCRIPT, hwnd);
            hr = this->InjectScript(L"browser", L"DOM.js", IDR_DOM_SCRIPT, hwnd);
            hr = this->InjectScript(L"browser", L"Runtime.js", IDR_RUNTIME_SCRIPT, hwnd);
            hr = this->InjectScript(L"browser", L"Page.js", IDR_PAGE_SCRIPT, hwnd);
            hr = this->InjectScript(L"browser", L"CSSParser.js", IDR_CSSPARSER_SCRIPT, hwnd);

            // Inject script  onto the debugger thread
            hr = this->InjectScript(L"debugger", L"Assert.js", IDR_ASSERT_SCRIPT, hwnd);
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

HRESULT WebSocketHandler::SendMessageToInstance(_In_ HWND& instanceHwnd, _In_ CString& message)
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

HRESULT WebSocketHandler::InjectScript(_In_ const LPCWSTR id, _In_ const LPCWSTR scriptName, _In_ const DWORD resourceId, _In_ HWND hwnd)
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