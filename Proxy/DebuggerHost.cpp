//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "DebuggerHost.h"

using namespace std::placeholders;

DebuggerHost::DebuggerHost() : 
    ScriptEngineHost()
{
    m_hBreakNotificationComplete.Attach(::CreateEvent(nullptr, TRUE, FALSE, nullptr));
    ATLENSURE_THROW(m_hBreakNotificationComplete.m_h != nullptr, E_UNEXPECTED);
}

HRESULT DebuggerHost::Initialize(_In_ HWND proxyHwnd, _In_ IRemoteDebugApplication* pRemoteDebugApplication)
{
    HRESULT hr = ScriptEngineHost::Initialize(proxyHwnd);
    FAIL_IF_NOT_S_OK(hr);

    // Scope for the script context
    {
        JsContextPtr context(m_scriptContext);

        JsValueRefPtr pGlobalObject;
        JsValueRefPtr pHostObject;
        hr = ScriptEngineHost::InitializeRuntime(&pGlobalObject, &pHostObject);

        // Add the javascript functions
        JsErrorCode jec = this->DefineCallback(pHostObject.m_value, L"postMessageToEngine", std::bind(&DebuggerHost::postMessageToEngine, this, _1, _2, _3, _4));
        FAIL_IF_ERROR(jec);

        // Add debugger component the script uses to control IE script debugging
        hr = InitializeDebuggerComponent(pGlobalObject.m_value, pRemoteDebugApplication);
        FAIL_IF_NOT_S_OK(hr);
    }

    return hr;
}

// Window Messages
LRESULT DebuggerHost::OnControllerCommand(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    ::PostMessage(m_proxyHwnd, nMsg, wParam, lParam);
    return 0;
}

LRESULT DebuggerHost::OnDebuggerCommand(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    HRESULT hr = S_OK;

    // Check if we need to do any processing of the message before forwarding it on to the debugger
    switch (nMsg)
    {
    case WM_DEBUGGINGDISABLED:
    {
        // Check the message flag for the detached state
        bool isDetached = !!lParam;
        if (isDetached)
        {
            // Since we are detached when this notification tells us the runtime is no longer
            // debugging, we know it is from part of the detach sequence so we can clean up
            // our references and close down
            this->FireEvent(L"closed", nullptr, 0);

            // We return directly instead of informing the Debugger, since we have already shut down
            return S_OK;
        }
    }
    break;

    default:
        // Other notification types should just be forwarded on to the debugger directly
        break;
    }

    if (m_spDebuggerDispatch.p != nullptr)
    {
        // Forward the message to the DebuggerDispatch
        m_spDebuggerDispatch->OnDebuggerNotification(nMsg, wParam, lParam);
    }

    return hr;
}

// JavaScript Functions
JsValueRef DebuggerHost::postMessageToEngine(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount)
{
    if (argumentCount == 4)
    {
        JsContextPtr context(m_scriptContext);

        // Grab the id of the engine
        const wchar_t* id;
        size_t idLength;
        JsErrorCode jec = ::JsStringToPointer(arguments[1], &id, &idLength);

        bool isAtBreakpoint;
        jec = ::JsBooleanToBool(arguments[2], &isAtBreakpoint);

        // Post the data to that engine
        const wchar_t* data;
        size_t dataLength;
        jec = ::JsStringToPointer(arguments[3], &data, &dataLength);

        unique_ptr<MessagePacket> spPacket(new MessagePacket());
        spPacket->m_engineId = id;
        spPacket->m_messageType = (isAtBreakpoint ? MessageType::ExecuteAtBreak : MessageType::Execute);
        spPacket->m_message = data;

        MessagePacket* pPacketParam = spPacket.release();
        BOOL succeeded = ::PostMessage(m_websocketHwnd, WM_MESSAGE_RECEIVE, reinterpret_cast<WPARAM>(pPacketParam), NULL);
        if (!succeeded)
        {
            spPacket.reset(pPacketParam);
        }
    }

    return JS_INVALID_REFERENCE;
}

// Helper Functions
HRESULT DebuggerHost::InitializeDebuggerComponent(_In_ JsValueRef globalObject, _In_ IRemoteDebugApplication* pRemoteDebugApplication)
{
    JsContextPtr context(m_scriptContext);

    // Grab the IDispatch from the chakra engine
    CComVariant globalVariant;
    JsErrorCode jec = ::JsValueToVariant(globalObject, &globalVariant);
    FAIL_IF_ERROR(jec);
    ATLENSURE_RETURN_HR(globalVariant.vt == VT_DISPATCH && globalVariant.pdispVal != nullptr, E_UNEXPECTED);

    CComPtr<IDispatch> spGlobalDispatch(globalVariant.pdispVal);
    CComQIPtr<IDispatchEx> spScriptGlobalNamespace(spGlobalDispatch);
    ATLENSURE_RETURN_HR(spScriptGlobalNamespace.p != nullptr, E_NOINTERFACE);

    // Load up the debugger dispatch
    CComObject<CDebuggerDispatch>* pDebuggerDispatch;
    HRESULT hr = CComObject<CDebuggerDispatch>::CreateInstance(&pDebuggerDispatch);
    FAIL_IF_NOT_S_OK(hr);

    m_spDebuggerDispatch = pDebuggerDispatch;
    hr = m_spDebuggerDispatch->Initialize(m_hWnd, spScriptGlobalNamespace, m_hBreakNotificationComplete, FALSE, pRemoteDebugApplication);
    FAIL_IF_NOT_S_OK(hr);

    CComVariant varDebug(m_spDebuggerDispatch);

    // Make a new debug object that the scripts can use to call into our debugger
    JsValueRef debugObject;
    jec = ::JsVariantToValue(&varDebug, &debugObject);
    FAIL_IF_ERROR(jec);

    JsPropertyIdRef debugPropertyId;
    jec = ::JsGetPropertyIdFromName(L"debug", &debugPropertyId);
    FAIL_IF_ERROR(jec);

    // Add the debug object to the global one
    jec = ::JsSetProperty(globalObject, debugPropertyId, debugObject, true);
    FAIL_IF_ERROR(jec);

    return S_OK;
}