//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

#include "ScriptEngineHost.h"

class DebuggerHost :
    public ScriptEngineHost
{
public:
    DebuggerHost();

    BEGIN_MSG_MAP(DebuggerHost)
        MESSAGE_RANGE_HANDLER(WM_CONTROLLERCOMMAND_FIRST, WM_CONTROLLERCOMMAND_LAST, OnControllerCommand)
        MESSAGE_RANGE_HANDLER(WM_DEBUGGERNOTIFICATION_FIRST, WM_DEBUGGERNOTIFICATION_LAST, OnDebuggerCommand)
        CHAIN_MSG_MAP(ScriptEngineHost)
    END_MSG_MAP()

    // Window Messages
    LRESULT OnControllerCommand(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);
    LRESULT OnDebuggerCommand(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);

    HRESULT Initialize(_In_ HWND proxyHwnd, _In_ IRemoteDebugApplication* pRemoteDebugApplication);

private:
    // JavaScript Functions
    JsValueRef postMessageToEngine(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount);

    // Helper Functions
    HRESULT InitializeDebuggerComponent(_In_ JsValueRef globalObject, _In_ IRemoteDebugApplication* pRemoteDebugApplication);

private:
    CHandle m_hBreakNotificationComplete;
    CComObjPtr<CDebuggerDispatch> m_spDebuggerDispatch;
};