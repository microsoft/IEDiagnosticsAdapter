//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "ThreadController.h"
#include "PDMThreadCallback.h"
#include "DebugThreadWindowMessages.h"
#include <algorithm>
#include "ad1ex.h"

// Ids start at 1 so that '0' can be classed as invalid
ULONG ThreadController::s_nextFrameId = 1;
ULONG ThreadController::s_nextPropertyId = 1;

// The maximum number of eval calls we can have running simultaneously,
// Exceeding this number will cause the current ones to abort so that the new one can run.
const int ThreadController::s_maxConcurrentEvalCount = 20;

// The identifier prepended to any script eval's to mark them as coming from the F12 debugger.
// The Q: property indicates the current size of the eval queue showing how many other pending evals are waiting.
const CString ThreadController::s_f12EvalIdentifierPrefix = L"/*F12Eval Q:";
const CString ThreadController::s_f12EvalIdentifierPrefixEnd = L"*/";
const CString ThreadController::s_f12EvalIdentifier = ThreadController::s_f12EvalIdentifierPrefix + L"%u" + ThreadController::s_f12EvalIdentifierPrefixEnd;

// public static XHR_BREAKPOINT_FLAG: string in breakpoint.ts needs to be kept in sync with this string in order to display the correct message in the call stack window when an XHR breakpoint is hit
const CComBSTR ThreadController::s_xhrBreakpointFlag = L"XmlHttpRequest response";

ThreadController::ThreadController() :
    m_dispatchThreadId(::GetCurrentThreadId()), // We are always created on the dispatch thread
    m_hwndDebugPipeHandler(nullptr),
    m_hBreakNotificationComplete(nullptr),
    m_isDocked(false),
    m_isConnected(false),
    m_mainIEScriptThreadId(0),
    m_currentPDMThreadId(0)
{
}

HRESULT ThreadController::Initialize(_In_ HWND hwndDebugPipeHandler, _In_ HANDLE hBreakNotificationComplete, _In_ bool isDocked, _In_ PDMEventMessageQueue* pMessageQueue, _In_ SourceController* pSourceController)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(hBreakNotificationComplete != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(hwndDebugPipeHandler != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(pMessageQueue != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(pSourceController != nullptr, E_INVALIDARG);

    m_hwndDebugPipeHandler = hwndDebugPipeHandler;
    m_hBreakNotificationComplete = hBreakNotificationComplete;
    m_isDocked = isDocked;
    m_spMessageQueue = pMessageQueue;
    m_spSourceController = pSourceController;
    
    return S_OK;
}

bool ThreadController::IsConnected() const
{
    // Can be called by either thread
    return m_isConnected;
}

bool ThreadController::IsAtBreak()
{
    // Can be called by either thread
    CComCritSecLock<CComAutoCriticalSection> lock(m_csThreadControllerLock);
    return (m_spCurrentBrokenThread.p != nullptr);
}

bool ThreadController::IsEnabled()
{
    // Can be called by either thread
    CComCritSecLock<CComAutoCriticalSection> lock(m_csThreadControllerLock);
    return (m_spDebugApplication.p != nullptr);
}

HRESULT ThreadController::SetRemoteDebugApplication(_In_ IRemoteDebugApplication* pDebugApplication)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pDebugApplication != nullptr, E_INVALIDARG);

    CComCritSecLock<CComAutoCriticalSection> lock(m_csThreadControllerLock);

    if (m_spDebugApplication == pDebugApplication)
    {
        // Already connected to this, so do nothing
        return S_OK;
    }

    m_isConnected = false;
    m_spDebugApplication = pDebugApplication;
    m_mainIEScriptThreadId = 0;

    // IRemoteDebugApplication110 is needed to grab the id for the UI thread
    CComQIPtr<IRemoteDebugApplication110> spRemoteDebugApp110(pDebugApplication);
    ATLENSURE_RETURN_HR(spRemoteDebugApp110.p != nullptr, E_NOINTERFACE);

    // Get the thread id for the main IE UI thread which is used to know if we need to pump messages at a break
    CComPtr<IRemoteDebugApplicationThread> spThread;
    HRESULT hr = spRemoteDebugApp110->GetMainThread(&spThread);
    if (hr == S_OK)
    {
        hr = spThread->GetSystemThreadId(&m_mainIEScriptThreadId);
        if (hr != S_OK)
        {
            // Default back to 0 (invalid id) if the call failed for some reason
            m_mainIEScriptThreadId = 0;
        }
    }

    return S_OK;
}

 HRESULT ThreadController::SetDockState(_In_ bool isDocked)
 {
     ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

     m_isDocked = isDocked;

     return S_OK;
 }

 HRESULT ThreadController::SetEnabledState(_In_ bool isEnabled)
 {
     ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

     // Set the enabled state of the source controller
     m_spSourceController->SetEnabledState(isEnabled);
     return S_OK;
 }

// Debugger Actions
HRESULT ThreadController::Connect()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    HRESULT hr = S_OK;

    if (this->IsConnected())
    {
        // Already connected so do nothing
        // We can get here if the front end and the BHOSite both try to cause a connection, so just ignore the double call.
    }
    else
    {
        // Get the IRemoteDebugApplication using our helper function that keeps it thread safe
        CComPtr<IRemoteDebugApplication> spDebugAppKeepAlive;
        hr = this->GetRemoteDebugApplication(spDebugAppKeepAlive);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Create thread event listener for listening to PDM debugger events
        CComObject<ThreadEventListener>* pThreadEventListener;
        hr = CComObject<ThreadEventListener>::CreateInstance(&pThreadEventListener);
        BPT_FAIL_IF_NOT_S_OK(hr);

        hr = pThreadEventListener->Initialize(this);
        BPT_FAIL_IF_NOT_S_OK(hr);

        m_spThreadEventListener = pThreadEventListener;

        // Populate sources before connecting
        // We need to call the enumeration on the debugger thread so that we act as if the PDM is firing the events normally
        // Calling Enumerate from the dispatch thread would cause locks to get obtained in the wrong order
        hr = this->CallDebuggerThread(PDMThreadCallbackMethod::Rundown, (DWORD_PTR)m_spSourceController.p, (DWORD_PTR)nullptr);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Connect up the IApplicationDebugger
        hr = spDebugAppKeepAlive->ConnectDebugger(pThreadEventListener);
        if (hr != S_OK)
        {
            // A debugger is already attached
            m_spThreadEventListener.Release();

            // We can still display and listen to source events due to the source rundown feature,
            // so we don't need to release our m_spDebugApp
            hr = E_ALREADY_ATTACHED;
        }
        else
        {
            // Enable support for user unhandled exceptions now that the debugger is attached
            CComQIPtr<IRemoteDebugApplication110> spDebugApp110 = spDebugAppKeepAlive;
            ATLENSURE_RETURN_HR(spDebugApp110.p != nullptr, E_NOINTERFACE);
            SCRIPT_DEBUGGER_OPTIONS options = SDO_ENABLE_NONUSER_CODE_SUPPORT;
            HRESULT hrOptions = spDebugApp110->SetDebuggerOptions(options, options);
            BPT_FAIL_IF_NOT_S_OK(hrOptions);
        }

        // Signal that we have attached the debugger (do this regardless of failure to unblock IE thread during JIT debugging)
        ::SendMessageW(m_hwndDebugPipeHandler, WM_WAITFORATTACH, NULL, /*startWaiting=*/ FALSE);

        m_isConnected = (hr == S_OK);
    }

    return hr;
}

HRESULT ThreadController::Disconnect()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    if (!this->IsConnected())
    {
        return S_OK;
    }

    HRESULT hr = S_OK;

    // Get the IRemoteDebugApplication using our helper function that keeps it thread safe
    CComPtr<IRemoteDebugApplication> spDebugAppKeepAlive;
    hr = this->GetRemoteDebugApplication(spDebugAppKeepAlive);
    BPT_FAIL_IF_NOT_S_OK(hr);

    // We need to check if spDebugAppKeepAlive is null, since the debugger may have been activated
    // while debugging was disabled (e.g.: after the profiler started).
    // If that happened, then m_spDebugApplication would be null when we tried to AddRef it.
    if (spDebugAppKeepAlive.p != nullptr)
    {
        // Unbind all the the breakboints and remove them from the engine so we don't hit any once we close (if debugging IE is enabled via checkbox)
        m_spSourceController->UnbindAllCodeBreakpoints(/*removeFromEngine=*/ true);
        m_spSourceController->UnbindAllEventBreakpoints(/*removeListeners=*/ true);

        // Disconnect the debugger first to prevent re-entrancy into handle breakpoint
        if (m_spThreadEventListener.p != nullptr) // If we don't have a listener then we only connected for sources, not with debugging
        {
            hr = spDebugAppKeepAlive->DisconnectDebugger();
            ATLASSERT(hr == S_OK); // Failure to disconnect, should still clean up the rest of the debugger
        }

        if (this->IsAtBreak())
        {
            hr = this->Resume(BREAKRESUMEACTION_CONTINUE);
            BPT_FAIL_IF_NOT_S_OK(hr);
        }
    }

    // Release the listener that handles the pdm callbacks
    if (m_spThreadEventListener.p != nullptr)
    {
        m_spThreadEventListener.Release();
    }

    // Clear all source nodes but don't release the Controller since we will re-use it on next connect
    m_spSourceController->Clear();

    // Do Release the IDebugApplication pointer, since we get a new one when we next connect
    m_spDebugApplication.Release();

    // Set our connected flag
    m_isConnected = false;

    return hr;
}

HRESULT ThreadController::Pause()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    if (!this->IsConnected() || this->IsAtBreak())
    {
        return S_OK;
    }

    // We need to call pause on the debugger thread so that we get the internal pdm locks in the correct order:
    // m_csCrossThreadCallLock then m_csBreakPoint
    // If we call CauseBreak directly, we will end up getting the locks in the reverse order which can cause a deadlock.
    HRESULT hr = this->CallDebuggerThread(PDMThreadCallbackMethod::Pause, (DWORD_PTR)nullptr, (DWORD_PTR)nullptr);
    
    // This could fail if we already disconnected, so don't assert, but do return the hr

    return hr;
}

HRESULT ThreadController::Resume(_In_ BREAKRESUMEACTION breakResumeAction, _In_ ERRORRESUMEACTION errorResumeAction)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    if (!this->IsConnected() || !this->IsAtBreak())
    {
        return E_NOT_VALID_STATE;
    }

    HRESULT hr = S_OK;

    // Scope for the CallFrame lock, the local smart pointer's addref will keep it alive after the lock
    CComPtr<IRemoteDebugApplicationThread> spDebugThreadKeepAlive;
    {
        // Lock the callstack, so that we are thread safe
        CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

        // Create a local smart pointer to addRef so that it won't be released by the other thread while we are using it
        spDebugThreadKeepAlive = m_spCurrentBrokenThread;
    }

    if (spDebugThreadKeepAlive.p != nullptr)
    {
        // Since we are about to resume, we clear all the current break information
        this->ClearBreakInfo();

        // By default we abort any errors so that exceptions execute the expected path.
        // If we want to support 'continue from exception' such as after using setNextStatement during an exception break, 
        // we should pass in ERRORRESUMEACTION_SkipErrorStatement,
        // But we should not do that by default as it changes the behavior of exceptions when running under the debugger and should only be
        // enabled through a user action.
        unique_ptr<ResumeFromBreakpointInfo> spResumeFromBreakpointInfo(new ResumeFromBreakpointInfo());
        spResumeFromBreakpointInfo->spDebugThread = spDebugThreadKeepAlive;
        spResumeFromBreakpointInfo->breakResumeAction = breakResumeAction;
        spResumeFromBreakpointInfo->errorResumeAction = errorResumeAction;

        // Get the IRemoteDebugApplication using our helper function that keeps it thread safe
        CComPtr<IRemoteDebugApplication> spDebugAppKeepAlive;
        hr = this->GetRemoteDebugApplication(spDebugAppKeepAlive);

        // We need to check if spDebugAppKeepAlive is null, since the debugger may have been activated
        // while debugging was disabled (e.g.: after the profiler started).
        // If that happened, then m_spDebugApplication would be null when we tried to AddRef it.
        if (hr == S_OK && spDebugAppKeepAlive.p != nullptr)
        {
            // Get the IDebugApplicationThread interface used to make the actual thread switch call
#ifdef _WIN64
            CComQIPtr<IDebugApplicationThread64> spDebugThread(spDebugThreadKeepAlive);
#else
            CComQIPtr<IDebugApplicationThread> spDebugThread(spDebugThreadKeepAlive);
#endif
            ATLENSURE_RETURN_HR(spDebugThread.p != nullptr, E_NOINTERFACE);

            CComQIPtr<IDebugApplication> spDebugApp(spDebugAppKeepAlive);
            ATLENSURE_RETURN_HR(spDebugApp.p != nullptr, E_NOINTERFACE);

            // Create an object for thread switching on to the debugger thread
            CComObject<PDMThreadCallback>* pCall;
            hr = CComObject<PDMThreadCallback>::CreateInstance(&pCall);
            BPT_FAIL_IF_NOT_S_OK(hr);

            // We need to make sure that all pending 'Eval' calls have been completed on the current thread before we resume, otherwise the PDM will attempt 
            // to dequeue and execute an eval after we have resumed, causing chakra to assert that the debugging session has changed (eval started on previous break)
            // Queuing a request onto the Sync Queue in the PDM will make sure that the Async evals have completed before we execute our resume.
            // This is because the PDM executes all Async requests before any Sync ones (see CProcessThread::DoRequest() - debugger\pdm\scpthred.cpp)
            CComObjPtr<PDMThreadCallback> spCall(pCall);
            hr = spDebugThread->SynchronousCallIntoThread(spCall, (DWORD)PDMThreadCallbackMethod::CompleteEvals, (DWORD_PTR)spDebugApp.p, (DWORD_PTR)nullptr);
            ATLASSERT(hr == S_OK); // We want to try a resume even if this fails (in retail bits)
        }

        // We need to call resume on the debugger thread so that we get the internal pdm locks in the correct order:
        // m_csCrossThreadCallLock then m_csBreakPoint
        // If we call ResumeFromBreakPoint directly, we will end up getting the locks in the reverse order which can cause a deadlock.
        hr = this->CallDebuggerThread(PDMThreadCallbackMethod::Resume, (DWORD_PTR)nullptr, (DWORD_PTR)spResumeFromBreakpointInfo.release());
            
        // This could fail if we already disconnected, so don't assert, but do return the hr
    }
    else
    {
        // We are no longer at a break
        return E_NOT_VALID_STATE;
    }

    // The synchronous call to resume can occasionally cause a PDM break, if that happens, we will still be at a break here, so don't update IE
    if (!this->IsAtBreak())
    {
        // Inform the BHO that we have resumed from the break
        // We need to use send message to make sure that the pipe handler has sent the message to the BHO before we
        // continue processing messages
        ::SendMessageW(m_hwndDebugPipeHandler, WM_BREAKMODECHANGED, /*IsAtBreak=*/ FALSE, NULL);
    }

    return hr;
}

HRESULT ThreadController::GetThreadDescriptions(_Inout_ vector<CComBSTR>& spThreadDescriptions)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    
    // This function is safe to call while we are not at a breakpoint,
    // So we don't need to make a call to IsAtBreak()
    if (!this->IsConnected())
    {
        return E_FAIL;
    }

    // Get the IRemoteDebugApplication using our helper function that keeps it thread safe
    CComPtr<IRemoteDebugApplication> spDebugAppKeepAlive;
    HRESULT hr = this->GetRemoteDebugApplication(spDebugAppKeepAlive);
    BPT_FAIL_IF_NOT_S_OK(hr);

    CComPtr<IEnumRemoteDebugApplicationThreads> spEnumThreads;
    hr = spDebugAppKeepAlive->EnumThreads(&spEnumThreads);
    BPT_FAIL_IF_NOT_S_OK(hr);

    // Enumerate each thread and get its description
    ULONG numFetched = 0;
    do
    {
        CComPtr<IRemoteDebugApplicationThread> spRDAThread;
        if (spEnumThreads->Next(1, &spRDAThread.p, &numFetched) != S_OK || spRDAThread.p == nullptr)
        {
            break;
        }

        // Get the description and add it to the array that we will return
        CComBSTR bstrDescription;
        CComBSTR bstrState;
        hr = spRDAThread->GetDescription(&bstrDescription, &bstrState);
        if (hr == S_OK)
        {
            spThreadDescriptions.push_back(bstrDescription);
        }
    } 
    while (numFetched != 0);

    return hr;
}

HRESULT ThreadController::SetBreakOnFirstChanceExceptions(_In_ bool breakOnFirstChance)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // This function is safe to call while we are not at a breakpoint,
    // So we don't need to make a call to IsAtBreak()
    if (!this->IsConnected())
    {
        return E_FAIL;
    }

    // Get the IRemoteDebugApplication using our helper function that keeps it thread safe
    CComPtr<IRemoteDebugApplication> spDebugAppKeepAlive;
    HRESULT hr = this->GetRemoteDebugApplication(spDebugAppKeepAlive);
    BPT_FAIL_IF_NOT_S_OK(hr);

    CComQIPtr<IRemoteDebugApplication110> spDebugApp110 = spDebugAppKeepAlive;
    ATLENSURE_RETURN_HR(spDebugApp110.p != nullptr, E_NOINTERFACE);

    hr = spDebugApp110->SetDebuggerOptions(SDO_ENABLE_FIRST_CHANCE_EXCEPTIONS, breakOnFirstChance ? SDO_ENABLE_FIRST_CHANCE_EXCEPTIONS : SDO_NONE);

    return hr;
}

HRESULT ThreadController::SetBreakOnNewWorker(_In_ bool enable)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // This function is safe to call while we are not at a breakpoint,
    // So we don't need to make a call to IsAtBreak()
    if (!this->IsConnected())
    {
        return E_FAIL;
    }

    m_spSourceController->SetBreakOnNewWorker(enable);

    return S_OK;
}

HRESULT ThreadController::SetNextEvent(_In_ const CString& eventBreakpointType)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    // Store the event break type for the next break (it will be an empty string for a regular async break)
    m_nextBreakEventType = eventBreakpointType;

    return S_OK;
}

// Break Mode Actions
HRESULT ThreadController::GetCurrentThreadDescription(_Out_ CComBSTR& threadDescription)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    
    // Lock the callframe access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    if (!this->IsConnected() || !this->IsAtBreak())
    {
        return E_NOT_VALID_STATE;
    }

    CComBSTR state;
    return m_spCurrentBrokenThread->GetDescription(&threadDescription, &state);
}

HRESULT ThreadController::GetBreakEventInfo(_Out_ shared_ptr<BreakEventInfo>& spBreakInfo)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the callframe access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    if (!this->IsConnected() || !this->IsAtBreak())
    {
        return E_NOT_VALID_STATE;
    }

    spBreakInfo = m_spCurrentBreakInfo;

    return S_OK;
}

HRESULT ThreadController::GetCallFrames(_Inout_ vector<shared_ptr<CallFrameInfo>>& spFrames)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the callframe access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    if (!this->IsConnected() || !this->IsAtBreak())
    {
        return E_NOT_VALID_STATE;
    }

    for (auto& frameId : m_callFrames)
    {
        auto it = m_callFramesInfoMap.find(frameId);
        if (it != m_callFramesInfoMap.end())
        {
            spFrames.push_back(it->second);
        }
    }

    return S_OK;
}

HRESULT ThreadController::GetLocals(_In_ ULONG frameId, _Out_ ULONG& propertyId)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the callframe access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    if (!this->IsConnected() || !this->IsAtBreak())
    {
        return E_NOT_VALID_STATE;
    }

    auto it = m_localsMap.find(frameId);
    if (it != m_localsMap.end())
    {
        propertyId = it->second;
    }
    else
    {
        return E_NOT_FOUND;
    }

    return S_OK;
}

HRESULT ThreadController::EnumeratePropertyMembers(_In_ ULONG propertyId, _In_ UINT radix, _Inout_ vector<shared_ptr<PropertyInfo>>& spPropertyInfos)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the callframe access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    if (!this->IsConnected() || !this->IsAtBreak())
    {
        return E_NOT_VALID_STATE;
    }

    auto it = m_propertyMap.find(propertyId);
    if (it != m_propertyMap.end())
    {
        CComPtr<IDebugProperty> spDebugProperty = it->second;

        // Get all the members for the children
        CComPtr<IEnumDebugPropertyInfo> spEnumProps;
        HRESULT hr = spDebugProperty->EnumMembers(PROP_INFO_ALL, radix, IID_IEnumDebugPropertyInfo, &spEnumProps);
        
        if (hr == E_NOTIMPL)
        {
            // This is reported in Watson, but we do not have a repro. Returning no properties is a 
            // better experience than failing, until we can diagnose and avoid this case.
            _ASSERT_EXPR(false, L"IEnumDebugPropertyInfo->EnumMembers returned E_NOTIMPL");
            return S_OK;
        }
        else if (hr == E_FAIL)
        {
            // The webcrawler hits an issue where the PDM returns E_FAIL. In this case we want to just
            // return an empty array back to the front end instead of asserting and blocking with a dialog.
            return S_OK;
        }

        BPT_FAIL_IF_NOT_S_OK(hr);

        hr = spEnumProps->Reset();
        BPT_FAIL_IF_NOT_S_OK(hr);

        ULONG count;
        hr = spEnumProps->GetCount(&count);
        if (hr == S_OK) // Since GetCount can fail when walking the properties, we return the hr which results in an empty array
        {
            for (ULONG i = 0; i < count; i++)
            {
                // Fetch each child one at a time, if we fail to get all of them, we can return with the successful ones
                CAutoVectorPtr<DebugPropertyInfo> spDebugPropertyInfo;
                bool alloced = spDebugPropertyInfo.Allocate(1);
                ATLENSURE_RETURN_HR(alloced == true, E_OUTOFMEMORY);

                // Fetch the next child and add it to the list
                ULONG numFetched = 0;
                hr = spEnumProps->Next(1, spDebugPropertyInfo.m_p, &numFetched);
                if (hr == S_OK && numFetched == 1) // This can fail for certain properties (e.g. location) in some conditions (e.g. a navigation), so we just ignore and continue
                {
                    // Get the property info
                    shared_ptr<PropertyInfo> spPropertyInfo;
                    hr = this->PopulatePropertyInfo(spDebugPropertyInfo[0], spPropertyInfo);
                    if (hr == S_OK)
                    {
                        // Store this to the out parameter
                        spPropertyInfos.push_back(spPropertyInfo);
                    }
                }
            }
        }
    }
    else
    {
        return E_NOT_FOUND;
    }

    return S_OK;
}

HRESULT ThreadController::SetPropertyValueAsString(_In_ ULONG propertyId, _In_ BSTR* pValue, _In_ UINT radix)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the callframe access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    if (!this->IsConnected() || !this->IsAtBreak())
    {
        return E_NOT_VALID_STATE;
    }

    HRESULT hr = S_OK;

    auto it = m_propertyMap.find(propertyId);
    if (it != m_propertyMap.end())
    {
        hr = it->second->SetValueAsString(*pValue, radix);
    }
    else
    {
        hr = E_NOT_FOUND;
    }

    return hr;
}

HRESULT ThreadController::Eval(_In_ ULONG frameId, _In_ const CComBSTR& evalString, _In_ ULONG radix, _Out_ shared_ptr<PropertyInfo>& spPropertyInfo)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the callframe access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    if (!this->IsConnected() || !this->IsAtBreak())
    {
        return E_NOT_VALID_STATE;
    }

    HRESULT hr = S_OK;

    if (m_evalCallbacks.size() >= ThreadController::s_maxConcurrentEvalCount)
    {
        // Since evaluating an eval call is relatively quick, having > max running concurrently,
        // probably means that there was a failure in one causing the others to queue up behind it.
        // If we get into this state we should abort the latest eval and we cannot process this new one.
        // This will free up the waiting message pump to see if it can unwind the stack further.
        m_evalCallbacks.back().first->Abort();
        m_evalCallbacks.back().second->Abort();

        return E_ABORT;
    }

    // To help diagnose chakra recursion issues (such as the above queued up evals) we append
    // an identifying string to the eval so that it will appear in debugging information.
    CString evalWithId;
    evalWithId.Format(ThreadController::s_f12EvalIdentifier, m_evalCallbacks.size());
    evalWithId.Append(evalString);

    auto it = m_debugFrameMap.find(frameId);
    if (it != m_debugFrameMap.end())
    {
        CComQIPtr<IDebugExpressionContext> spEvalContext(it->second);
        ATLENSURE_RETURN_HR(spEvalContext.p != nullptr, E_NOINTERFACE);

        CComPtr<IDebugExpression> spDebugExpression;
        hr = spEvalContext->ParseLanguageText(evalWithId, radix, L"" /* not actually used, but required */, DEBUG_TEXT_RETURNVALUE | DEBUG_TEXT_ISEXPRESSION, &spDebugExpression);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Free the lock as we no longer use the eval context (spEvalContext) past this point,
        // This will allow the PDM thread to update the callstacks on its thread.
        lock.Unlock();

        // Local scope for COM objects.
        {
            // Keep our reference alive until all evals have finished (or been aborted)
            CComObjPtr<ThreadController> spKeepAlive(this);

            // Create expression callback
            CComObject<EvalCallback>* pDebugCallback;
            CComObject<EvalCallback>::CreateInstance(&pDebugCallback);
            ATLENSURE_RETURN_HR(pDebugCallback != nullptr, E_NOINTERFACE);
            
            CComObjPtr<EvalCallback> spDebugCallback(pDebugCallback);
            hr = spDebugCallback->Initialize();
            BPT_FAIL_IF_NOT_S_OK(hr);

            // Add this to our list so we can abandon it if we resume or close before it completes
            m_evalCallbacks.push_back(::make_pair(spDebugExpression, spDebugCallback));

            // Evaluate the expression
            hr = spDebugExpression->Start(spDebugCallback);
            BPT_FAIL_IF_NOT_S_OK(hr);

            hr = spDebugCallback->WaitForCompletion();

            // Remove from our list
            ATLENSURE_RETURN_HR(m_evalCallbacks.back().first == spDebugExpression, E_UNEXPECTED);
            m_evalCallbacks.pop_back();

            // Check if we were closed down before this evaluation completed, if so just return abort failure
            if (hr == E_ABORT)
            {
                return hr;
            }
        }

        HRESULT returnResult;
        CComPtr<IDebugProperty> spDebugProperty;

        // Determine if the expression evaluation was successful
        hr = spDebugExpression->GetResultAsDebugProperty(&returnResult, &spDebugProperty);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // The get result function should always succeed, but the actual result from the eval could fail if
        // chakra was unable to process the request. If that is the case we should return the failure out
        // to the caller, which will result in sending null to the JavaScript which is handled correctly.
        if (returnResult != S_OK)
        {
            return returnResult;
        }

        DebugPropertyInfo propInfo;
        hr = spDebugProperty->GetPropertyInfo(PROP_INFO_ALL, radix, &propInfo);
        if (hr == S_OK)
        {
            hr = this->PopulatePropertyInfo(propInfo, spPropertyInfo);
        }
        else
        {
            return E_NOT_FOUND;
        }
    }
    else
    {
        return E_NOT_FOUND;
    }

    return hr;
}

HRESULT ThreadController::CanSetNextStatement(_In_ ULONG docId, _In_ ULONG start)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the callframe access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    if (!this->IsConnected() || !this->IsAtBreak())
    {
        return E_NOT_VALID_STATE;
    }

    HRESULT hr = S_OK;

    // We use the current call frame, which should be the first one in the m_callFrames list
    ULONG firstFrameId = (m_callFrames.empty() ? 0 : m_callFrames.front());

    auto it = m_debugFrameMap.find(firstFrameId);
    if (it != m_debugFrameMap.end())
    {
        // Get the code context for the passed in map
        // If it is not found, we fail gracefully (no assert) to the caller
        CComPtr<IDebugCodeContext> spDebugCodeContext;
        hr = m_spSourceController->GetCodeContext(docId, start, spDebugCodeContext);
        if (hr == S_OK) // This could fail due to requiring a refresh (the caller will handle the hresult)
        {
            CComQIPtr<IDebugStackFrame> spStackFrame(it->second);
            ATLENSURE_RETURN_HR(spStackFrame.p != nullptr, E_NOINTERFACE);

            CComQIPtr<ISetNextStatement> spSetter(spStackFrame);
            ATLENSURE_RETURN_HR(spSetter.p != nullptr, E_NOINTERFACE);

            hr = spSetter->CanSetNextStatement(spStackFrame, spDebugCodeContext);
        }
    }
    else
    {
        return E_NOT_FOUND;
    }

    return hr;
}

HRESULT ThreadController::SetNextStatement(_In_ ULONG docId, _In_ ULONG start)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the callframe access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    if (!this->IsConnected() || !this->IsAtBreak())
    {
        return E_NOT_VALID_STATE;
    }

    HRESULT hr = S_OK;

    // We use the current call frame, which should be the first one in the m_callFrames list
    ULONG firstFrameId = (m_callFrames.empty() ? 0 : m_callFrames.front());

    auto it = m_debugFrameMap.find(firstFrameId);
    if (it != m_debugFrameMap.end())
    {
        // Get the code context for the passed in map
        // If it is not found, we fail gracefully (no assert) to the caller
        CComPtr<IDebugCodeContext> spDebugCodeContext;
        hr = m_spSourceController->GetCodeContext(docId, start, spDebugCodeContext);
        if (hr == S_OK) // This could fail due to requiring a refresh (the caller will handle the hresult)
        {
            CComQIPtr<IDebugStackFrame> spStackFrame(it->second);
            ATLENSURE_RETURN_HR(spStackFrame.p != nullptr, E_NOINTERFACE);

            CComQIPtr<ISetNextStatement> spSetter(spStackFrame);
            ATLENSURE_RETURN_HR(spSetter.p != nullptr, E_NOINTERFACE);

            hr = spSetter->SetNextStatement(spStackFrame, spDebugCodeContext);
        }
    }
    else
    {
        return E_NOT_FOUND;
    }

    return hr;
}

// PDM Event Notifications
HRESULT ThreadController::OnPDMBreak(_In_ IRemoteDebugApplicationThread* pRDAThread, _In_ BREAKREASON br, _In_ const CComBSTR& description, _In_ WORD errorId, _In_ bool isFirstChance, _In_ bool isUserUnhandled)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    // Get the new call stack information
    m_spCurrentBrokenThread = pRDAThread;
    this->PopulateCallStack();

    // If the callframe list is empty, we were not given any information about this break, so default to invalid id (0)
    ULONG firstFrameId = (m_callFrames.empty() ? 0 : m_callFrames.front());

    // Set our new break state
    m_spCurrentBreakInfo.reset(new BreakEventInfo());
    m_spCurrentBreakInfo->firstFrameId = firstFrameId;
    m_spCurrentBreakInfo->errorId = errorId;
    m_spCurrentBreakInfo->breakReason = br;
    m_spCurrentBreakInfo->description = description;
    m_spCurrentBreakInfo->isFirstChanceException = isFirstChance;
    m_spCurrentBreakInfo->isUserUnhandled = isUserUnhandled;

    // Get the PDM thread id of the one that was actually broken
    HRESULT hr = m_spCurrentBrokenThread->GetSystemThreadId(&m_currentPDMThreadId);
    if (hr != S_OK) 
    {
        m_currentPDMThreadId = 0;
    }

    m_spCurrentBreakInfo->systemThreadId = m_currentPDMThreadId;

    // If this is caused by a breakpoint, we need to gather further information.
    // Any condition needs to be evaluated on the other thread since we cannot continue or evaluate
    // synchronously during the onHandleBreakpoint function. 
    // Doing so causes a deadlock in the PDM.
    if (br == BREAKREASON_BREAKPOINT)
    {
        // Find the code breakpoint
        auto it = m_callFramesInfoMap.find(firstFrameId);
        if (it != m_callFramesInfoMap.end())
        {
            shared_ptr<BreakpointInfo> spBreakpointInfo;
            const SourceLocationInfo& sourceLocation = it->second->sourceLocation;
            hr = m_spSourceController->GetBreakpointInfoForBreak(sourceLocation.docId, sourceLocation.charPosition, spBreakpointInfo);
            if (hr == S_OK)
            {
                m_spCurrentBreakInfo->breakpoints.push_back(spBreakpointInfo);
            }
        }
    }
    else if (!m_nextBreakEventType.IsEmpty())
    {
        if (br == BREAKREASON_DEBUGGER_HALT)
        {
            // Find the event breakpoints for this type
            vector<shared_ptr<BreakpointInfo>> breakpointList;
            hr = m_spSourceController->GetBreakpointInfosForEventBreak(m_nextBreakEventType, breakpointList);
            if (hr == S_OK)
            {
                m_spCurrentBreakInfo->breakpoints = std::move(breakpointList);
            }

            m_spCurrentBreakInfo->breakReason = BREAKREASON_BREAKPOINT;
        }

        m_spCurrentBreakInfo->breakEventType = m_nextBreakEventType;

        // If this breakpoint was created with the Create XHR Breakpoint button, we need to tell the frontend that an XHR breakpoint was hit in order to display appropriate strings
        if (m_nextBreakEventType == L"readystatechange") 
        {
            for (auto it = m_spCurrentBreakInfo->breakpoints.begin(); it != m_spCurrentBreakInfo->breakpoints.end(); ++it) 
            {
                if (std::find((**it).spEventTypes->begin(), (**it).spEventTypes->end(), s_xhrBreakpointFlag) != (**it).spEventTypes->end()) 
                {
                    m_spCurrentBreakInfo->breakEventType = s_xhrBreakpointFlag;
                }
            }
        }
    }

    // Inform the DebuggerDispatch about the event
    m_spMessageQueue->Push(PDMEventType::BreakpointHit);

    return S_OK;
}

HRESULT ThreadController::OnPDMClose()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    // Inform the DebuggerDispatch about the event
    m_spMessageQueue->Push(PDMEventType::PDMClosed);

    return S_OK;
}

HRESULT ThreadController::OnWaitCallback()
{
    return this->WaitForBreakNotification();
}

// Helper functions
HRESULT ThreadController::GetRemoteDebugApplication(_Out_ CComPtr<IRemoteDebugApplication>& spRemoteDebugApplication)
{
    // This can be called by either thread

    // Lock the access to the IRemoteDebugApp so that we are thread safe
    CComCritSecLock<CComAutoCriticalSection> lock(m_csThreadControllerLock);

    // Cause the out param smart pointer to addRef so that it won't be released by the other thread while we are using it
    spRemoteDebugApplication = m_spDebugApplication;

    return (spRemoteDebugApplication.p != nullptr ? S_OK : E_NOT_VALID_STATE);
}

HRESULT ThreadController::CallDebuggerThread(_In_ PDMThreadCallbackMethod method, _In_opt_ DWORD_PTR pController, _In_opt_ DWORD_PTR pArgs)
{
    // Get the IRemoteDebugApplication using our helper function that keeps it thread safe
    CComPtr<IRemoteDebugApplication> spDebugAppKeepAlive;
    HRESULT hr = this->GetRemoteDebugApplication(spDebugAppKeepAlive);

    // We need to check if spDebugAppKeepAlive is null, since the debugger may have been activated
    // while debugging was disabled (e.g.: after the profiler started).
    // If that happened, then m_spDebugApplication would be null when we tried to AddRef it.
    if (hr == S_OK && spDebugAppKeepAlive.p != nullptr)
    {
        // Get the IDebugApplication interface used to make the actual thread switch call
        CComQIPtr<IDebugApplication> spDebugApp(spDebugAppKeepAlive);
        ATLENSURE_RETURN_HR(spDebugApp.p != nullptr, E_NOINTERFACE);

        // Create an object for thread switching on to the debugger thread
        CComObject<PDMThreadCallback>* pCall;
        hr = CComObject<PDMThreadCallback>::CreateInstance(&pCall);
        BPT_FAIL_IF_NOT_S_OK(hr);

        if (pController == NULL)
        {
            // Use the IDebugApplication by default
            pController = (DWORD_PTR)spDebugApp.p;
        }

        // Begin the call into the debugger thread using the supplied parameters
        CComObjPtr<PDMThreadCallback> spCall(pCall);
        hr = spDebugApp->SynchronousCallInDebuggerThread(spCall, (DWORD)method, (DWORD_PTR)pController, (DWORD_PTR)pArgs);
        ATLASSERT(hr == S_OK);
    }

    return hr;
}

HRESULT ThreadController::PopulateCallStack()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(this->IsAtBreak(), E_NOT_VALID_STATE);

    // Lock the access to the callstack so that we are thread safe
    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    CComPtr<IEnumDebugStackFrames> pStackFrameEnum;
    HRESULT hr = m_spCurrentBrokenThread->EnumStackFrames(&pStackFrameEnum);
    if (hr == S_OK)
    {
        if ((hr = pStackFrameEnum->Reset()) != S_OK)
        {
            return hr;
        }

        shared_ptr<DebugStackFrameDescriptor> spFrameDescriptor(new DebugStackFrameDescriptor());
        ULONG nFetched;
        for (ULONG ulIndex = 0; (pStackFrameEnum->Next(1, spFrameDescriptor.get(), &nFetched) == S_OK) && (nFetched == 1); ulIndex++)
        {
            // Get a new identifier for this source node
            ULONG newFrameId = ThreadController::CreateUniqueFrameId();
            ULONG newPropertyId = ThreadController::CreateUniquePropertyId();

            // Create object that represents the source file
            CComObject<CallFrame>* pFrame;
            hr = CComObject<CallFrame>::CreateInstance(&pFrame);
            BPT_FAIL_IF_NOT_S_OK(hr);

            hr = pFrame->Initialize(m_dispatchThreadId, newFrameId, spFrameDescriptor, m_spSourceController);
            BPT_FAIL_IF_NOT_S_OK(hr);

            // Add to our maps
            m_callFrameMap[newFrameId] = pFrame;
            m_callFrames.push_back(newFrameId);

            // Get the information about this call frame
            shared_ptr<CallFrameInfo> spFrameInfo;
            CComPtr<IDebugStackFrame> spStackFrame;
            CComPtr<IDebugProperty> spLocalsDebugProperty;
            hr = pFrame->GetCallFrameInfo(spFrameInfo, spStackFrame, spLocalsDebugProperty);
            BPT_FAIL_IF_NOT_S_OK(hr);

            // Store that info in our maps
            m_callFramesInfoMap[newFrameId] = spFrameInfo;
            m_debugFrameMap[newFrameId] = spStackFrame;
            m_propertyMap[newPropertyId] = spLocalsDebugProperty;

            // Link the locals to the frame id for later lookup
            m_localsMap[newFrameId] = newPropertyId;
        }
    }

    return S_OK;
}

HRESULT ThreadController::PopulatePropertyInfo(_Inout_ DebugPropertyInfo& propInfo, _Out_ shared_ptr<PropertyInfo>& spPropertyInfo)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(propInfo.m_pDebugProp != nullptr, E_INVALIDARG);

    // Store this property for later enumeration
    ULONG newPropertyId = ThreadController::CreateUniquePropertyId();
    m_propertyMap[newPropertyId] = propInfo.m_pDebugProp;

    // Create the info and assign to the out param
    spPropertyInfo.reset(new PropertyInfo());
    spPropertyInfo->propertyId = newPropertyId;

    // Use attach to ensure we free the actual BSTR from the PDM when we are finished
    spPropertyInfo->type.Attach(((propInfo.m_dwValidFields & DBGPROP_INFO_TYPE) == DBGPROP_INFO_TYPE) ? propInfo.m_bstrType : ::SysAllocString(L""));
    spPropertyInfo->value.Attach(((propInfo.m_dwValidFields & DBGPROP_INFO_VALUE) == DBGPROP_INFO_VALUE) ? propInfo.m_bstrValue : ::SysAllocString(L""));

    spPropertyInfo->isExpandable =  ((propInfo.m_dwAttrib & DBGPROP_ATTRIB_VALUE_IS_EXPANDABLE) == DBGPROP_ATTRIB_VALUE_IS_EXPANDABLE);
    spPropertyInfo->isReadOnly =    ((propInfo.m_dwAttrib & DBGPROP_ATTRIB_VALUE_READONLY) == DBGPROP_ATTRIB_VALUE_READONLY);
    spPropertyInfo->isFake =        ((propInfo.m_dwAttrib & DBGPROP_ATTRIB_VALUE_IS_FAKE) == DBGPROP_ATTRIB_VALUE_IS_FAKE);
    spPropertyInfo->isInvalid =     ((propInfo.m_dwAttrib & DBGPROP_ATTRIB_VALUE_IS_INVALID) == DBGPROP_ATTRIB_VALUE_IS_INVALID);
    spPropertyInfo->isReturnValue = ((propInfo.m_dwAttrib & DBGPROP_ATTRIB_VALUE_IS_RETURN_VALUE) == DBGPROP_ATTRIB_VALUE_IS_RETURN_VALUE);


    // Calculate the name if it has one
    spPropertyInfo->name.Attach(((propInfo.m_dwValidFields & DBGPROP_INFO_NAME) == DBGPROP_INFO_NAME) ? propInfo.m_bstrName : ::SysAllocString(L""));

    // Remove /*F12Eval Q:<id>*/ from name, if present
    CString name(spPropertyInfo->name);
    if (name.Find(ThreadController::s_f12EvalIdentifierPrefix) == 0 &&
        name.GetLength() >= ThreadController::s_f12EvalIdentifierPrefix.GetLength() + ThreadController::s_f12EvalIdentifierPrefixEnd.GetLength() + 1)
    {
        name = name.Mid(name.Find(ThreadController::s_f12EvalIdentifierPrefixEnd) + ThreadController::s_f12EvalIdentifierPrefixEnd.GetLength());
        spPropertyInfo->name.Attach(name.AllocSysString());
    }

    // Calculate the full name if it has one
    spPropertyInfo->fullName.Attach(((propInfo.m_dwValidFields & DBGPROP_INFO_FULLNAME) == DBGPROP_INFO_FULLNAME) ? propInfo.m_bstrFullName : ::SysAllocString(L""));

    // Remove /*F12Eval Q:<id>*/ from fullname, if present
    CString fullName(spPropertyInfo->fullName);
    if (fullName.Find(ThreadController::s_f12EvalIdentifierPrefix) == 0 &&
        fullName.GetLength() >= ThreadController::s_f12EvalIdentifierPrefix.GetLength() + ThreadController::s_f12EvalIdentifierPrefixEnd.GetLength() + 1)
    {
        fullName = fullName.Mid(fullName.Find(ThreadController::s_f12EvalIdentifierPrefixEnd) + ThreadController::s_f12EvalIdentifierPrefixEnd.GetLength());
        spPropertyInfo->fullName.Attach(fullName.AllocSysString());
    }

    return S_OK;
}

HRESULT ThreadController::ClearBreakInfo()
{
    // Lock the callstack, so that we are thread safe
    CComCritSecLock<CComAutoCriticalSection> lock(m_csCallFramesLock);

    // Abandon any pending evals since they will now never complete
    for (auto& itPair : m_evalCallbacks)
    {
        // Abort this call in the PDM
        itPair.first->Abort();

        // Stop waiting for the response
        itPair.second->Abort();
    }

    // Since we just resumed from the breakpoint, clear out all the information that was stored about the previous break,
    // Also ensure to release the IRemoteDebugApplicationThread so that the PDM can dispose of it.
    m_callFrames.clear();
    m_callFramesInfoMap.clear();
    m_callFrameMap.clear();
    m_localsMap.clear();
    m_debugFrameMap.clear();
    m_propertyMap.clear();

    m_spCurrentBrokenThread.Release();
    
    return S_OK;
}

HRESULT ThreadController::BeginBreakNotification()
{
    // This needs to be called from the debugger dispatch due to an issue in the IE pause notification.
    // Since the IE pause UI is drawn with no timeout, it will flash on/off everytime we hit a break that gets resumed straight away
    // For example during a conditional breakpoint with a false condition.
    // To prevent the flash we call this asynchronously from the debugger dispatch instead of synchronously from our OnPDMBreak event
    // This does mean that there is a possibility of a deadlock occuring when we are docked and the IE threads are still tied to together after hitting a break.
    // The current version has this issue, but we require a change in IE (to separate the pause UI from the thread decoupling) to allow us to call this
    // synchronously with our ODM break event.

    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED); // Should really be synchronous with PDM thread (see comment above)

    // This gets called on the pdm thread, but it makes a synchronous call to the IE thread to notify it of the break.
    // When IE has finished processing the break, m_hBreakNotificationComplete gets signalled.
    // During this wait, we need to process very specific messages to allow docked debugging to work correctly.

    HRESULT hr = S_OK;

    // Reset the event used by the notification
    ::ResetEvent(m_hBreakNotificationComplete);

    // We need to use send message to make sure that the pipe handler has sent the message to the BHO 
    // before we continue and start waiting on the break event
    // This prevents a freeze where we haven't managed to tell IE about the break before we start waiting for it on this thread.
    ::SendMessageW(m_hwndDebugPipeHandler, WM_BREAKMODECHANGED, /*IsAtBreak=*/ TRUE, NULL);

    if (m_isDocked)
    {
        // Get the IRemoteDebugApplication using our helper function that keeps it thread safe
        CComPtr<IRemoteDebugApplication> spDebugAppKeepAlive;
        hr = this->GetRemoteDebugApplication(spDebugAppKeepAlive);
        if (hr == S_OK)
        {
            // While docked we also need to pump focus messages on the IE UI thread so that the IE Frame has chance to detach from the tab's input queue
            CComQIPtr<IDebugApplication110> spDebugApp110 = spDebugAppKeepAlive;
            ATLENSURE_RETURN_HR(spDebugApp110 != nullptr, E_NOINTERFACE);

            // We should only pump messages if we have stopped on the UI thread. If we attempt to call the PDM thread switch onto the UI thread
            // when we are not broken on the UI thread, we will cause a deadlock since all other (non-broken) threads are halted.
            if (m_mainIEScriptThreadId == m_currentPDMThreadId && m_mainIEScriptThreadId != 0)
            {
                // Create a callback that we can use to switch threads in the PDM
                CComObject<PDMThreadCallback>* pCall;
                hr = CComObject<PDMThreadCallback>::CreateInstance(&pCall);
                BPT_FAIL_IF_NOT_S_OK(hr);

                CComObjPtr<PDMThreadCallback> spCall(pCall);

                // We need to use the synchronous call because PDM processes ALL async calls before ANY sync calls (opposite of NT).
                // If we were to use the async mechanism, we could get into a state where synch calls never get a chance to run on the UI thread.
                hr = spDebugApp110->SynchronousCallInMainThread(spCall, (DWORD_PTR)PDMThreadCallbackMethod::WaitForBreak, (DWORD_PTR)this, (DWORD_PTR)nullptr);
                BPT_FAIL_IF_NOT_S_OK(hr);
            }
        }
    }

    return hr;
}

HRESULT ThreadController::WaitForBreakNotification()
{
    // Process messages on the IE thread so that F12 doesn't get blocked waiting for a synchronous call to finish.
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED); 

    // Since we are sending the break notification directly from our debugger backend here (instead of via the frame) 
    // we don't want to process our messages until the 'break complete' event has been triggered.
    HRESULT hr = S_OK;
    while (hr == S_OK)
    {
        // Since the IE tab and frame had their input queues synchronized before the break, there may be pending messages
        // on the frame's queue that are blocking us from working while docked.
        // We need to dispose of these messages so that the tab and frame's threads can be detached by the BHO.
        DWORD dwWaitResult = ::MsgWaitForMultipleObjects(1, &m_hBreakNotificationComplete, FALSE, INFINITE, QS_SENDMESSAGE);
        if (dwWaitResult != WAIT_FAILED)
        {
            if (dwWaitResult == WAIT_OBJECT_0)
            {
                break;
            }
            else
            {
                // There was a message put into our queue, so we need to dispose of it
                MSG msg;
                while (::PeekMessage(&msg, NULL, WM_KILLFOCUS, WM_KILLFOCUS, PM_REMOVE))
                {
                    ::TranslateMessage(&msg);
                    ::DispatchMessage(&msg);
                    // Check if the event was triggered before pumping more messages
                    if (::WaitForSingleObject(m_hBreakNotificationComplete, 0) == WAIT_OBJECT_0)
                    {
                        break;
                    }
                }
            }
        }
        else
        {
            hr = ::AtlHresultFromLastError();
        }
    }

    return hr;
}
