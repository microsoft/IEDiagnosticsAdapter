//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

#include "ScriptEngineHost.h"
#include <MsHTML.h>
#include <ExDispid.h>
#include <mshtmldiagnostics.h>

#define BROWSER_EVENTS_ID   250

class BrowserHost :
    public CComObjectRootEx<CComSingleThreadModel>,
    public CWindowImpl<BrowserHost>,
    public IDispEventImpl<BROWSER_EVENTS_ID, BrowserHost, &DIID_DWebBrowserEvents2, &LIBID_SHDocVw, 1, 1>,
    public IDiagnosticsScriptEngineSite
{
public:
    BrowserHost();
    ~BrowserHost();

    BEGIN_COM_MAP(BrowserHost)
        COM_INTERFACE_ENTRY(IDiagnosticsScriptEngineSite)
    END_COM_MAP()

    BEGIN_MSG_MAP(BrowserHost)
        MESSAGE_HANDLER(WM_SET_MESSAGE_HWND, OnSetMessageHwnd)
        MESSAGE_HANDLER(WM_MESSAGE_RECEIVE, OnMessageReceived)
    END_MSG_MAP()

    BEGIN_SINK_MAP(BrowserHost)
        SINK_ENTRY_EX(BROWSER_EVENTS_ID, DIID_DWebBrowserEvents2, DISPID_NAVIGATECOMPLETE2, &BrowserHost::DWebBrowserEvents2_NavigateComplete2)
    END_SINK_MAP()

    // IDiagnosticsScriptEngineSite
    STDMETHOD(OnMessage)(_In_reads_(ulDataCount)  LPCWSTR* pszData, ULONG ulDataCount);
    STDMETHOD(OnScriptError)(_In_ IActiveScriptError* pScriptError);

    // Window Messages
    LRESULT OnSetMessageHwnd(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);
    LRESULT OnMessageReceived(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);

    // DWebBrowserEvents2
    STDMETHOD_(void, DWebBrowserEvents2_NavigateComplete2)(LPDISPATCH pDisp, VARIANT * URL);

    HRESULT Initialize(_In_ HWND proxyHwnd, _In_ IUnknown* pWebControl);

private:
    HRESULT OnNavigateCompletePrivate(_In_ CComPtr<IUnknown>& spUnknown, _In_ CString& url);

private:
    HWND m_proxyHwnd;
    HWND m_websocketHwnd;
    CComPtr<IUnknown> m_spWebControl;
    CComPtr<IDiagnosticsScriptEngine> m_spDiagnosticsEngine;
    bool m_enableDebuggingInjectedCode;
};

