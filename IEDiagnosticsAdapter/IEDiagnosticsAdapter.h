//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

#include "Proxy_h.h"
#include "WebSocketHandler.h"
#include <memory>
#include <iostream>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

class IEDiagnosticsAdapter :
    public CWindowImpl < IEDiagnosticsAdapter >
{
public:
    IEDiagnosticsAdapter(_In_ LPCWSTR rootPath);
    ~IEDiagnosticsAdapter();

    BEGIN_MSG_MAP(WebSocketClient)
        MESSAGE_HANDLER(WM_COPYDATA, OnCopyData);
        MESSAGE_HANDLER(WM_PROCESSCOPYDATA, OnMessageFromIE)
    END_MSG_MAP()

    // Window Messages
    LRESULT OnCopyData(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);
    LRESULT OnMessageFromIE(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);
private:
    shared_ptr<WebSocketHandler> m_webSocketHander;
};