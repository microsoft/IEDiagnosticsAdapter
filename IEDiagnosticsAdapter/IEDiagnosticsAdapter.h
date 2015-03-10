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

    IEInstance(UUID guid, DWORD processId, HWND hwnd, LPCWSTR url, LPCWSTR title, LPCWSTR filePath) :
        guid(guid),
        processId(processId),
        hwnd(hwnd),
        url(url),
        title(title),
        filePath(filePath),
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

class IEDiagnosticsAdapter :
    public CWindowImpl<IEDiagnosticsAdapter>
{
public:
    IEDiagnosticsAdapter(_In_ LPCWSTR rootPath);
    ~IEDiagnosticsAdapter();

    BEGIN_MSG_MAP(WebSocketClient)
        MESSAGE_HANDLER(WM_POLL_WEBSOCKET, OnPollWebSocket)
        MESSAGE_HANDLER(WM_COPYDATA, OnCopyData);
        MESSAGE_HANDLER(WM_PROCESSCOPYDATA, OnMessageFromIE)
    END_MSG_MAP()

    // Window Messages
    LRESULT OnPollWebSocket(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);
    LRESULT OnCopyData(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);
    LRESULT OnMessageFromIE(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);

private:
    // Helper functions
    void Poll();
    HRESULT PopulateIEInstances();
    HRESULT ConnectToInstance(_In_ IEInstance& instance);
    HRESULT SendMessageToInstance(_In_ HWND& instanceHwnd, _In_ CString& message);
    HRESULT InjectScript(_In_ const LPCWSTR id, _In_ const LPCWSTR scriptName, _In_ const DWORD resourceId, _In_ HWND hwnd);

    // WebSocket Callbacks
    void OnHttp(websocketpp::connection_hdl hdl);
    bool OnValidate(websocketpp::connection_hdl hdl);
    void OnOpen(websocketpp::connection_hdl hdl);
    void OnMessage(websocketpp::connection_hdl hdl, server::message_ptr msg);
    void OnClose(websocketpp::connection_hdl hdl);

private:
    CString m_rootPath;
    server m_server;
    DWORD m_port;
    map<HWND, IEInstance> m_instances;
    map<websocketpp::connection_hdl, HWND, owner_less<websocketpp::connection_hdl>> m_clientConnections;
    map<HWND, websocketpp::connection_hdl> m_proxyConnections;
};