//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

#include "Proxy_h.h"
#include "JsWrappers.h"
#include "../DebuggerCore/DebuggerDispatch.h"

typedef function<JsValueRef(JsValueRef, bool, JsValueRef*, unsigned short)> JsCallback;

class ScriptEngineHost :
    public CWindowImpl<ScriptEngineHost>
{
public:
    ScriptEngineHost();
    ~ScriptEngineHost();

    BEGIN_MSG_MAP(ScriptEngineHost)
        MESSAGE_HANDLER(WM_SET_MESSAGE_HWND, OnSetMessageHwnd)
        MESSAGE_HANDLER(WM_MESSAGE_RECEIVE, OnMessageReceived)
    END_MSG_MAP()

    // Window Messages
    LRESULT OnSetMessageHwnd(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);
    LRESULT OnMessageReceived(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);

    HRESULT Initialize(_In_ HWND proxyHwnd);
    HRESULT InitializeRuntime(_Out_opt_ JsValueRefPtr* pGlobalObject, _Out_opt_ JsValueRefPtr* pHostObject);

protected:
    // Host Helper Functions
    JsErrorCode DefineCallback(JsValueRef globalObject, const wchar_t* callbackName, _In_ const JsCallback& callbackFunc);
    void FireEvent(_In_ const wchar_t* eventName,
        _In_reads_(argumentCount) JsValueRef* arguments,
        _In_ const unsigned short argumentCount,
        _In_opt_ function<void(JsValueRef returnValue)> returnCallback = [](JsValueRef){});

    void ExecuteScript(_In_ const LPCWSTR& fileName, _In_ const LPCWSTR& script);

private:
    // JavaScript Functions
    JsValueRef alert(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount);
    JsValueRef addEventListener(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount);
    JsValueRef removeEventListener(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount);
    JsValueRef postMessage(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount);

    // Helper Functions
    template<class TFunction>
    void IterateListeners(_Inout_ list<pair<JsValueRefPtr, JsValueRefPtr>>& listeners, TFunction func);

    HRESULT StartDebugging();

protected:
    HWND m_proxyHwnd;
    HWND m_websocketHwnd;
    JsRuntimeHandle m_scriptRuntime;
    JsContextRef m_scriptContext;
    JsSourceContext m_currentSourceContext;

private:
    bool m_enableDebuggingInjectedCode;
    map<wstring, JsCallback> m_hostCallbacks;

    size_t m_currentIterations;
    map<CString, list<pair<JsValueRefPtr, JsValueRefPtr>>> m_eventHandlers;
};

