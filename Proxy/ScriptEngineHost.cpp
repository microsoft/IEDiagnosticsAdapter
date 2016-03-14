//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "ScriptEngineHost.h"

using namespace std::placeholders;

ScriptEngineHost::ScriptEngineHost() :
    m_websocketHwnd(0),
    m_currentSourceContext(0),
    m_enableDebuggingInjectedCode(true)
{
    // Create our message window used to handle window messages
    HWND createdWindow = this->Create(HWND_MESSAGE);
    ATLENSURE_THROW(createdWindow != NULL, E_FAIL);
}

ScriptEngineHost::~ScriptEngineHost()
{
    if (::IsWindow(m_hWnd))
    {
        this->DestroyWindow();
    }
}

HRESULT ScriptEngineHost::Initialize(_In_ HWND proxyHwnd)
{
    m_proxyHwnd = proxyHwnd;

    // Create the Chakra runtime used for running script
    JsErrorCode jec = ::JsCreateRuntime(JsRuntimeAttributeNone, nullptr, &m_scriptRuntime);
    FAIL_IF_ERROR(jec);

    jec = ::JsCreateContext(m_scriptRuntime, &m_scriptContext);
    FAIL_IF_ERROR(jec);

    return S_OK;
}

HRESULT ScriptEngineHost::InitializeRuntime(_Out_opt_ JsValueRefPtr* pGlobalObject, _Out_opt_ JsValueRefPtr* pHostObject)
{
    // Scope for the script context
    {
        JsContextPtr context(m_scriptContext);

        if (m_enableDebuggingInjectedCode)
        {
            HRESULT hr = this->StartDebugging();
            ATLASSERT(hr == S_OK); hr; // Continue in retail bits even if we can't start debugging
        }

        // Get the global script object
        JsValueRef globalObject;
        JsErrorCode jec = ::JsGetGlobalObject(&globalObject);
        FAIL_IF_ERROR(jec);

        // Add an alert function to the global scope
        jec = this->DefineCallback(globalObject, L"alert", std::bind(&ScriptEngineHost::alert, this, _1, _2, _3, _4));
        FAIL_IF_ERROR(jec);

        // Make a new host object that the scripts can use to call into our c++
        JsValueRef hostObject;
        jec = ::JsCreateObject(&hostObject);
        FAIL_IF_ERROR(jec);

        JsPropertyIdRef hostPropertyId;
        jec = ::JsGetPropertyIdFromName(L"host", &hostPropertyId);
        FAIL_IF_ERROR(jec);

        // Add functions to the host object
        jec = this->DefineCallback(hostObject, L"addEventListener", std::bind(&ScriptEngineHost::addEventListener, this, _1, _2, _3, _4));
        FAIL_IF_ERROR(jec);
        jec = this->DefineCallback(hostObject, L"removeEventListener", std::bind(&ScriptEngineHost::removeEventListener, this, _1, _2, _3, _4));
        FAIL_IF_ERROR(jec);
        jec = this->DefineCallback(hostObject, L"postMessage", std::bind(&ScriptEngineHost::postMessage, this, _1, _2, _3, _4));
        FAIL_IF_ERROR(jec);

        // Add the host object to the global one
        jec = ::JsSetProperty(globalObject, hostPropertyId, hostObject, true);
        FAIL_IF_ERROR(jec);

        // Attach the out parameters
        if (pGlobalObject != nullptr)
        {
            *pGlobalObject = globalObject;
        }

        if (pHostObject != nullptr)
        {
            *pHostObject = hostObject;
        }
    }

    return S_OK;
}

// Window Messages
LRESULT ScriptEngineHost::OnSetMessageHwnd(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    // Store the HWND we will use for posting back to the proxy server
    m_websocketHwnd = reinterpret_cast<HWND>(lParam);

    return 0;
}

LRESULT ScriptEngineHost::OnMessageReceived(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    // Take ownership of the data
    unique_ptr<MessagePacket> spPacket(reinterpret_cast<MessagePacket*>(wParam));

    JsErrorCode jec = JsNoError;

    // Scope for the script context
    {
        JsContextPtr context(m_scriptContext);

        // Check if this is a script injection message
        if (spPacket->m_messageType == MessageType::Inject)
        {
            // Execute the script so it gets injected into our Chakra runtime
            this->ExecuteScript(spPacket->m_scriptName, spPacket->m_message);

            // Done
            return 0;
        }

        // Otherwise we fire the 'onmessage' event to any functions that are listening
        const WORD argCount = 1;
        JsValueRef args[argCount];
        jec = ::JsPointerToString(spPacket->m_message.m_str, spPacket->m_message.Length(), &args[0]);
        //ATLASSERT(jec == JsNoError);

        if (jec != JsNoError)
        {
            JsValueRef exception;
            ::JsGetAndClearException(&exception);

            JsPropertyIdRef messageName;
            ::JsGetPropertyIdFromName(L"message", &messageName);

            JsValueRef messageValue;
            ::JsGetProperty(exception, messageName, &messageValue);

            const wchar_t *message;
            size_t length;
            ::JsStringToPointer(messageValue, &message, &length);

            CString log;
            log.Format(L"chakrahost: exception: %ls\n", message);
            ::OutputDebugString(log);
        }

        this->FireEvent(L"onmessage", args, argCount);
    }

    return 0;
}

// JavaScript Functions
JsValueRef ScriptEngineHost::alert(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount)
{
    if (argumentCount == 2)
    {
        JsContextPtr context(m_scriptContext);

        const wchar_t* message;
        size_t length;
        JsErrorCode jec = ::JsStringToPointer(arguments[1], &message, &length);

        // Show the alert message as a dialog so we can use this for temporary debugging
        ::MessageBox(NULL, message, L"alert", 0);
    }

    return JS_INVALID_REFERENCE;
}

JsValueRef ScriptEngineHost::addEventListener(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount)
{
    if (argumentCount == 3)
    {
        JsContextPtr context(m_scriptContext);

        const wchar_t* eventName;
        size_t length;
        JsErrorCode jec = ::JsStringToPointer(arguments[1], &eventName, &length);

        // Store the listener for this event
        m_eventHandlers[eventName].push_back(pair<JsValueRefPtr, JsValueRefPtr>(arguments[0], arguments[2]));
    }

    return JS_INVALID_REFERENCE;
}

JsValueRef ScriptEngineHost::removeEventListener(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount)
{
    if (argumentCount == 3)
    {
        JsContextPtr context(m_scriptContext);

        const wchar_t* eventName;
        size_t length;
        JsErrorCode jec = ::JsStringToPointer(arguments[1], &eventName, &length);

        // Find out if we have any listeners for this event
        if (m_eventHandlers.find(eventName) != m_eventHandlers.end())
        {
            // Remove the matching listeners
            pair<JsValueRefPtr, JsValueRefPtr> toRemove(arguments[0], arguments[2]);
            auto removeMe = find(m_eventHandlers[eventName].begin(), m_eventHandlers[eventName].end(), toRemove);
            if (removeMe != m_eventHandlers[eventName].end())
            {
                if (m_currentIterations == 0)
                {
                    // Only remove from the list if we aren't currently running through firing events
                    m_eventHandlers[eventName].erase(removeMe);
                }
                else
                {
                    // Mark this listener for deletion since we are currently iterating through the listeners array
                    removeMe->first.Release();
                    removeMe->second.Release();
                }
            }
        }
    }

    return JS_INVALID_REFERENCE;
}

JsValueRef ScriptEngineHost::postMessage(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount)
{
    if (argumentCount == 2)
    {
        JsContextPtr context(m_scriptContext);

        const wchar_t* data;
        size_t length;
        JsErrorCode jec = ::JsStringToPointer(arguments[1], &data, &length);

        CComBSTR messageBstr(data);
        BSTR param = messageBstr.Detach();
        BOOL succeeded = ::PostMessage(m_websocketHwnd, WM_MESSAGE_SEND, reinterpret_cast<WPARAM>(param), 0);
        if (!succeeded)
        {
            messageBstr.Attach(param);
        }
    }

    return JS_INVALID_REFERENCE;
}

// Host Helper Functions
JsErrorCode ScriptEngineHost::DefineCallback(JsValueRef globalObject, const wchar_t* callbackName, _In_ const JsCallback& callbackFunc)
{
    ATLENSURE_RETURN_VAL(m_hostCallbacks.find(callbackName) == m_hostCallbacks.end(), JsErrorCode::JsErrorInvalidArgument);

    // Store the host callback
    m_hostCallbacks[callbackName] = callbackFunc;

    // Create the JavaScript callable function
    JsValueRef functionObject;
    JsErrorCode jec = ::JsCreateFunction([](JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount, void* callbackState) -> JsValueRef {
        return (*(JsCallback*)callbackState)(callee, isConstructCall, arguments, argumentCount);
    }, (void*)&m_hostCallbacks[callbackName], &functionObject);
    FAIL_IF_ERROR(jec);

    // Add it to the JavaScript object
    JsPropertyIdRef propertyId;
    jec = ::JsGetPropertyIdFromName(callbackName, &propertyId);
    FAIL_IF_ERROR(jec);

    jec = ::JsSetProperty(globalObject, propertyId, functionObject, true);
    FAIL_IF_ERROR(jec);

    return JsNoError;
}

void ScriptEngineHost::FireEvent(_In_ const wchar_t* eventName, 
                                 _In_reads_(argumentCount) JsValueRef* arguments, 
                                 _In_ const unsigned short argumentCount,
                                 _In_opt_ function<void(JsValueRef returnValue)> returnCallback)
{
    // Check we have at least one listener
    if (m_eventHandlers.find(eventName) != m_eventHandlers.end())
    {
        // Call each event handler for this event passing in the correct arguments
        this->IterateListeners(m_eventHandlers[eventName], [&, arguments, argumentCount, returnCallback](const pair<JsValueRefPtr, JsValueRefPtr>& listener) -> void {
            JsContextPtr context(m_scriptContext);

            // Prepend the 'this' object as the first argument
            vector<JsValueRef> finalArgs;
            finalArgs.push_back(listener.first.m_value);
            for (int i = 0; i < argumentCount; i++)
            {
                finalArgs.push_back(arguments[i]);
            }

            // Call the actual script function
            JsValueRef returnValue;
            JsErrorCode jec = ::JsCallFunction(listener.second.m_value, &finalArgs[0], static_cast<unsigned short>(finalArgs.size()), &returnValue);
            if (jec != JsNoError)
            {
                JsValueRef exception;
                ::JsGetAndClearException(&exception);

                JsPropertyIdRef messageName;
                ::JsGetPropertyIdFromName(L"message", &messageName);

                JsValueRef messageValue;
                ::JsGetProperty(exception, messageName, &messageValue);

                const wchar_t *message;
                size_t length;
                ::JsStringToPointer(messageValue, &message, &length);

                CString log;
                log.Format(L"chakrahost: exception: %ls\n", message);
                ::OutputDebugString(log);
            }

            // Process the return value if we need to
            returnCallback(&returnValue);
        });
    }
}

void ScriptEngineHost::ExecuteScript(_In_ const LPCWSTR& fileName, _In_ const LPCWSTR& script)
{
    // Execute the script so it gets injected into our Chakra runtime
    JsContextPtr context(m_scriptContext);

    JsValueRef returnValue;
    JsErrorCode jec = ::JsRunScript(script, m_currentSourceContext++, fileName, &returnValue);
    if (jec != JsNoError)
    {
        JsValueRef exception;
        ::JsGetAndClearException(&exception);

        JsPropertyIdRef messageName;
        ::JsGetPropertyIdFromName(L"message", &messageName);

        JsValueRef messageValue;
        ::JsGetProperty(exception, messageName, &messageValue);

        const wchar_t *message;
        size_t length;
        ::JsStringToPointer(messageValue, &message, &length);

        CString log;
        log.Format(L"%ls: exception: %ls\n", fileName, message);
        ::OutputDebugString(log);
    }
}

// Helper Functions
template<class TFunction>
void ScriptEngineHost::IterateListeners(_Inout_ list<pair<JsValueRefPtr, JsValueRefPtr>>& listeners, TFunction func)
{
    // Mark the listener iteration so that we don't invalidate the collection if an event handler calls removeListener
    m_currentIterations++;
    for_each(listeners.begin(), listeners.end(), [&func](const pair<JsValueRefPtr, JsValueRefPtr>& listener) -> void
    {
        if (listener.first.m_value != nullptr && listener.second.m_value != nullptr)
        {
            pair<JsValueRefPtr, JsValueRefPtr> value(listener);
            func(listener);
        }
    });
    m_currentIterations--;

    if (m_currentIterations == 0)
    {
        // Remove any listeners that are marked for deletion
        listeners.remove_if([](const pair<JsValueRefPtr, JsValueRefPtr>& listener) -> bool
        {
            return (listener.first.m_value == nullptr || listener.second.m_value == nullptr);
        });
    }
}

HRESULT ScriptEngineHost::StartDebugging()
{
    CComPtr<IClassFactory> spClassFactory;
    HRESULT hr = ::CoGetClassObject(__uuidof(ProcessDebugManager), CLSCTX_INPROC_SERVER, NULL, __uuidof(IClassFactory), (LPVOID *)&spClassFactory.p);
    FAIL_IF_NOT_S_OK(hr);

    CComPtr<IProcessDebugManager> spPDM;
    hr = spClassFactory->CreateInstance(0, _uuidof(IProcessDebugManager), (LPVOID *)&spPDM);
    FAIL_IF_NOT_S_OK(hr);

    CComPtr<IDebugApplication> spDebugApp;
    hr = spPDM->GetDefaultApplication(&spDebugApp);
    if (hr == S_OK || hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) // IE might have already started debugging for us
    {
        JsErrorCode jec = ::JsStartDebugging();
        FAIL_IF_ERROR(jec);
    }

    return S_OK;
}
