//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

#include "Proxy_h.h"
#include <iostream>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

typedef websocketpp::server<websocketpp::config::asio> server;

struct IEInstance
{
    UUID guid;
    DWORD processId;
    HWND hwnd;
    CString url;
    CString title;
    CString filePath;
    bool isConnected;
    CComPtr<IOleWindow> spSite;
    HWND connectionHwnd;
    bool is64BitTab;

    IEInstance(UUID guid, DWORD processId, HWND hwnd, LPCWSTR url, LPCWSTR title, LPCWSTR filePath, BOOL is64BitTab) :
        guid(guid),
        processId(processId),
        hwnd(hwnd),
        url(url),
        title(title),
        filePath(filePath),
        is64BitTab(!!is64BitTab),
        isConnected(false),
        connectionHwnd(0)
    {
    }

    IEInstance() :
        guid(GUID_NULL),
        processId(0),
        hwnd(0),
        url(L""),
        title(L""),
        filePath(L""),
        isConnected(false),
        connectionHwnd(0)
    {
    }
};

class WebSocketHandler
{
public:
    WebSocketHandler(_In_ LPCWSTR rootPath, _In_ HWND m_hWnd);
    void RunServer();

    // Windows messages that IEDiagnosticsAdapter will receive and parse, then have WebSocketHandler manage
    void OnMessageFromIE(string message, HWND proxyHwnd);

private:
    // Helper functions
    HRESULT PopulateIEInstances();
    HRESULT ConnectToInstance(_In_ IEInstance& instance);
    HRESULT SendMessageToInstance(_In_ HWND& instanceHwnd, _In_ CString& message);
    HRESULT InjectScript(_In_ const LPCWSTR id, _In_ const LPCWSTR scriptName, _In_ const DWORD resourceId, _In_ HWND hwnd);

    // window message handlers
    void OnMessageFromIEHandler(string message, HWND proxyHwnd);

    // WebSocket Callbacks
    void OnHttp(websocketpp::connection_hdl hdl);
    bool OnValidate(websocketpp::connection_hdl hdl);
    void OnOpen(websocketpp::connection_hdl hdl);
    void OnMessage(websocketpp::connection_hdl hdl, server::message_ptr msg);
    void OnClose(websocketpp::connection_hdl hdl);

private:
    server m_server;
    HWND m_hWnd;
    CString m_rootPath;
    DWORD m_port;
    map<HWND, IEInstance> m_instances;
    map<websocketpp::connection_hdl, HWND, owner_less<websocketpp::connection_hdl>> m_clientConnections;
    map<HWND, websocketpp::connection_hdl> m_proxyConnections;
};