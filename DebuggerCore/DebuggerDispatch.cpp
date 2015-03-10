//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "DebuggerDispatch.h"
#include "ScriptHelpers.h"

CDebuggerDispatch::CDebuggerDispatch() : 
    m_dispatchThreadId(::GetCurrentThreadId()), // We are always created on the dispatch thread
    m_eventHelper(this->GetUnknown()),
    m_hasHitBreak(false)
{
}

// IF12DebuggerExtension
STDMETHODIMP CDebuggerDispatch::Initialize(_In_ HWND hwndDebugPipeHandler, _In_ IDispatchEx* pScriptDispatchEx, _In_ HANDLE hBreakNotificationComplete, _In_ BOOL isDocked, _In_ IUnknown* pDebugApplication)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(hwndDebugPipeHandler != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(hBreakNotificationComplete != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(pScriptDispatchEx != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(pDebugApplication != nullptr, E_INVALIDARG);

    m_hwndDebugPipeHandler = hwndDebugPipeHandler;
    m_spScriptDispatch = pScriptDispatchEx;

    // Create the message queue used for getting the PDM events
    CComObject<PDMEventMessageQueue>* pMessageQueue;
    HRESULT hr = CComObject<PDMEventMessageQueue>::CreateInstance(&pMessageQueue);
    BPT_FAIL_IF_NOT_S_OK(hr);

    hr = pMessageQueue->Initialize(m_hwndDebugPipeHandler);
    BPT_FAIL_IF_NOT_S_OK(hr);

    m_spMessageQueue = pMessageQueue;

    // Create the source controller
    CComObject<SourceController>* pSourceController;
    hr = CComObject<SourceController>::CreateInstance(&pSourceController);
    BPT_FAIL_IF_NOT_S_OK(hr);

    hr = pSourceController->Initialize(m_spMessageQueue, m_hwndDebugPipeHandler);
    BPT_FAIL_IF_NOT_S_OK(hr);

    m_spSourceController = pSourceController;

    // Create the thread controller
    CComObject<ThreadController>* pThreadController;
    hr = CComObject<ThreadController>::CreateInstance(&pThreadController);
    BPT_FAIL_IF_NOT_S_OK(hr);

    hr = pThreadController->Initialize(m_hwndDebugPipeHandler, hBreakNotificationComplete, !!isDocked, m_spMessageQueue, m_spSourceController);
    BPT_FAIL_IF_NOT_S_OK(hr);

    m_spThreadController = pThreadController;

    // Call the helper function so that we also get the main thread id which is needed when hitting a breakpoint
    SetRemoteDebugApplication(pDebugApplication);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::Deinitialize()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    m_spScriptDispatch.Release();
    m_spThreadController.Release();
    m_spSourceController.Release();
    m_spMessageQueue.Release();

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::OnDebuggerNotification(_In_ const UINT notification, _In_ const UINT_PTR wParam, _In_ const LONG_PTR lParam)
{
    HRESULT hr = S_OK;

    switch (notification) 
    {
    case WM_PROCESSDEBUGGERPACKETS:
        {
            hr = this->FireQueuedEvents();
        }
        break;

    case WM_DEBUGGINGENABLED:
        {
            // Update the PDM state with the new pointer
            if (lParam != NULL)
            {
                IUnknown* pData = reinterpret_cast<IUnknown*>(lParam);
                CComQIPtr<IRemoteDebugApplication> spRemoteDebugApplication(pData);
                if (spRemoteDebugApplication.p != nullptr) 
                {
                    hr = this->SetRemoteDebugApplication(spRemoteDebugApplication);
                }
            }

            bool succeeded = !!wParam;

            // Notify DebugProvider.ts
            const UINT argLength = 1;
            CComVariant params[argLength];
            params[0] = succeeded;
            this->m_eventHelper.Fire_Event(L"debuggingenabled", params, argLength);
        }
        break;

    case WM_DEBUGGINGDISABLED:
        {
            // Inform the source controller that debugging is disabled, so 
            // we can unbind breakpoints, if any
            this->DebuggingDisabled();

            // Notify DebugProvider.ts
            this->m_eventHelper.Fire_Event(L"debuggingdisabled", nullptr, 0);
        }
        break;

    case WM_DOCKEDSTATECHANGED:
        {
            // Update the docking state
            bool isDocked = !!wParam;
            hr = this->DockedStateChanged(isDocked);
        }
        break;

    case WM_DEBUGAPPLICATIONCREATE:
        {
            bool isProfiling = !!wParam;

            // Notify DebugProvider.ts
            const UINT argLength = 1;
            CComVariant params[argLength];
            params[0] = isProfiling;
            this->m_eventHelper.Fire_Event(L"debugapplicationcreate", params, argLength);
        }
        break;

    case WM_WEBWORKERSTARTED:
        {
            DWORD workerId = static_cast<DWORD>(wParam);

            CComBSTR workerLabel;
            workerLabel.Attach((const BSTR)lParam);

            // Notify DebugProvider.ts
            const UINT argLength = 2;
            CComVariant params[argLength];
            params[1] = workerLabel;
            params[0] = workerId;
            this->m_eventHelper.Fire_Event(L"webworkerstarted", params, argLength);
        }
        break;

    case WM_WEBWORKERFINISHED:
        {
            DWORD workerId = static_cast<DWORD>(wParam);

            // Notify DebugProvider.ts
            const UINT argLength = 1;
            CComVariant params[argLength];
            params[0] = workerId;
            this->m_eventHelper.Fire_Event(L"webworkerfinished", params, argLength);
        }
        break;

    case WM_EVENTBREAKPOINT:
        {
            bool isStarting = !!wParam;
            CString eventType((const BSTR)lParam);

            hr = this->OnEventBreakpoint(isStarting, eventType);
        }
        break;

    case WM_EVENTLISTENERUPDATED:
        {
            bool isListening = !!wParam;

            hr = this->OnEventListenerUpdated(isListening);
        }
        break;

    case WM_BREAKNOTIFICATIONEVENT:
    case WM_WAITFORATTACH:
        // Do nothing for these events since they are handled in the DebugPipeHandler
        break;

    default:
        // Unknown notification
        hr = E_INVALIDARG;
    }

    BPT_FAIL_IF_NOT_S_OK(hr);
    return hr;
}

HRESULT CDebuggerDispatch::SetRemoteDebugApplication(_In_ IUnknown* pDebugApplication)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pDebugApplication != nullptr, E_INVALIDARG);

    // Forward the call onto the thread controller so that it can update its internal state
    CComQIPtr<IRemoteDebugApplication> spRemoteDebugApp(pDebugApplication);
    ATLENSURE_RETURN_HR(spRemoteDebugApp.p != nullptr, E_NOINTERFACE);

    HRESULT hr = m_spSourceController->SetRemoteDebugApplication(spRemoteDebugApp);
    BPT_FAIL_IF_NOT_S_OK(hr);

    return m_spThreadController->SetRemoteDebugApplication(spRemoteDebugApp);
}

HRESULT CDebuggerDispatch::DockedStateChanged(_In_ BOOL isDocked)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Tell the thread controller about the dock state change
    return m_spThreadController->SetDockState(!!isDocked);
}

HRESULT CDebuggerDispatch::DebuggingDisabled()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // This function is called from the DebugEngineSite when profiling has caused debugging to become disabled
    // Since this caused chakra to delete all of the breakpoint information it had, we need to clear out
    // our own stored information so that we can re-bind correctly.
    m_spThreadController->SetEnabledState(false);

    return S_OK;
}

HRESULT CDebuggerDispatch::FireQueuedEvents()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    if (m_spMessageQueue.p != nullptr && m_spThreadController.p != nullptr)
    {
        vector<PDMEventType> messages;
        m_spMessageQueue->PopAll(messages);

        for (auto& eventType : messages)
        {
            switch (eventType)
            {
            case PDMEventType::SourceUpdated:
                {
                    // Source documents have changed, so request the new info, and inform the JavaScript
                    vector<shared_ptr<DocumentInfo>> added;
                    vector<shared_ptr<DocumentInfo>> updated;
                    vector<ULONG> removed;
                    vector<shared_ptr<ReboundBreakpointInfo>> rebound;
                    HRESULT hr = m_spSourceController->RefreshSources(added, updated, removed, rebound);
                    BPT_FAIL_IF_NOT_S_OK(hr);

                    // Added
                    if (!added.empty())
                    {
                        vector<CComVariant> addedDocuments;
                        for (const auto& node : added)
                        {
                            map<CString, CComVariant> properties;
                            properties[L"docId"] = node->docId;
                            properties[L"parentDocId"] =  node->parentId;
                            properties[L"url"] = node->url;
                            properties[L"mimeType"] = node->mimeType;
                            properties[L"length"] = node->textLength;
                            properties[L"isDynamicCode"] =  node->isDynamicCode;
                            properties[L"sourceMapUrlFromHeader"] = node->sourceMapUrlFromHeader;
                            properties[L"longDocumentId"] = node->longDocumentId;

                            CComVariant object;
                            hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, properties, object);
                            ATLASSERT(hr == S_OK); // Assert in chk bits so we can tell if this fails. In ret bits, just continue with the next member instead

                            if (hr == S_OK)
                            {
                                addedDocuments.push_back(object);
                            }
                        }

                        this->FireDocumentEvent(L"onAddDocuments", addedDocuments);
                    }

                    // Updated
                    if (!updated.empty())
                    {
                        vector<CComVariant> updatedDocuments;
                        for (const auto& node : updated)
                        {
                            map<CString, CComVariant> properties;
                            properties[L"docId"] = node->docId;
                            properties[L"url"] = node->url;
                            properties[L"mimeType"] = node->mimeType;
                            properties[L"length"] = node->textLength;

                            CComVariant object;
                            hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, properties, object);
                            ATLASSERT(hr == S_OK); // Assert in chk bits so we can tell if this fails. In ret bits, just continue with the next member instead

                            if (hr == S_OK)
                            {
                                updatedDocuments.push_back(object);
                            }
                        }

                        this->FireDocumentEvent(L"onUpdateDocuments", updatedDocuments);
                    }

                    // Removed
                    if (!removed.empty())
                    {
                        vector<CComVariant> removedDocuments;
                        for (const auto& docId : removed)
                        {
                            removedDocuments.push_back(CComVariant(docId));
                        }

                        this->FireDocumentEvent(L"onRemoveDocuments", removedDocuments);
                    }

                    // Rebound breakpoints
                    if (!rebound.empty())
                    {
                        vector<CComVariant> reboundBreakpoints;
                        for (const auto& node : rebound)
                        {
                            map<CString, CComVariant> properties;
                            properties[L"breakpointId"] = node->breakpointId;
                            properties[L"newDocId"] = node->newDocId;
                            properties[L"start"] = node->start;
                            properties[L"length"] = node->length;
                            properties[L"isBound"] = node->isBound;

                            CComVariant object;
                            hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, properties, object);
                            ATLASSERT(hr == S_OK); // Assert in chk bits so we can tell if this fails. In ret bits, just continue with the next member instead

                            if (hr == S_OK)
                            {
                                reboundBreakpoints.push_back(object);
                            }
                        }

                        this->FireDocumentEvent(L"onResolveBreakpoints", reboundBreakpoints);
                    }
                }
                break;

            case PDMEventType::BreakpointHit:
                {
                    // A breakpoint was hit, so inform the JavaScript
                    // The JavaScript should also perform evaluation of any conditional so that we know to break or not
                    shared_ptr<BreakEventInfo> spBreakInfo;
                    HRESULT hr = m_spThreadController->GetBreakEventInfo(spBreakInfo);
                    ATLENSURE_RETURN_HR(hr == S_OK || hr == E_NOT_VALID_STATE, hr);

                    // Since we may have already resumed before
                    if (hr == E_NOT_VALID_STATE)
                    {
                        break;
                    }

                    m_hasHitBreak = true;

                    CComVariant breakpointsArray;
                    if (spBreakInfo->breakReason == BREAKREASON_BREAKPOINT)
                    {
                        // Create the breakpoint specific infos
                        vector<CComVariant> allBreakpoints;
                        if (spBreakInfo->breakpoints.empty())
                        {
                            // If we have no breakpoints registered at the location that the PDM broke into,
                            // we need to insert a dummy breakpoint to make sure that the front end is still notified
                            // of the break. It will end up showing the current callstack, but with limited location info.
                            spBreakInfo->breakpoints.push_back(shared_ptr<BreakpointInfo>(new BreakpointInfo()));
                        }

                        for (const auto& bp : spBreakInfo->breakpoints)
                        {
                            CComVariant breakpointObject;
                            hr = this->CreateScriptBreakpointInfo(*bp, breakpointObject);
                            ATLASSERT(hr == S_OK); // In retail bits we just ignore the failed breakpoint
                            if (hr == S_OK)
                            {
                                allBreakpoints.push_back(breakpointObject);
                            }
                        }

                        hr = ScriptHelpers::CreateJScriptArray(m_spScriptDispatch, allBreakpoints, breakpointsArray);
                        BPT_FAIL_IF_NOT_S_OK(hr);
                    }
                    else
                    {
                        // Null for no breakpoint
                        breakpointsArray.ChangeType(VT_NULL, nullptr);
                    }

                    map<CString, CComVariant> properties;
                    properties[L"firstFrameId"] = spBreakInfo->firstFrameId;
                    properties[L"errorId"] = spBreakInfo->errorId;
                    properties[L"breakReason"] = spBreakInfo->breakReason;
                    properties[L"description"] = spBreakInfo->description;
                    properties[L"isFirstChanceException"] = spBreakInfo->isFirstChanceException;
                    properties[L"isUserUnhandled"] = spBreakInfo->isUserUnhandled;
                    properties[L"breakpoints"] = breakpointsArray;
                    properties[L"systemThreadId"] = spBreakInfo->systemThreadId;
                    properties[L"breakEventType"] = spBreakInfo->breakEventType;

                    CComVariant object;
                    hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, properties, object);
                    BPT_FAIL_IF_NOT_S_OK(hr);

                    const UINT argLength = 1;
                    CComVariant params[argLength] = { object };

                    bool isRealBreak = false;
                    m_eventHelper.Fire_Event(L"onBreak", params, argLength, [&isRealBreak](const CComVariant *returnValue) {
                        if (!isRealBreak)
                        {
                            isRealBreak = (returnValue->vt == VT_BOOL && returnValue->boolVal == VARIANT_TRUE);
                        }
                    });

                    // Since non-real breaks get resumed from straight away (example conditional breakpoint with a false condition),
                    // We only inform IE about the break if the break is going to require the user to manually continue.
                    // This should really be done synchronously with the PDM break event, but due to an issue with the pause overlay in IE,
                    // we need to do this only if the break is not resumed, to avoid flashing the pause UI when a break is resumed fast.
                    if (isRealBreak)
                    {
                        m_spThreadController->BeginBreakNotification();
                    }

                }
                break;

            case PDMEventType::PDMClosed: 
                {
                    // Disconnect
                    HRESULT hr = m_spThreadController->Disconnect();
                    BPT_FAIL_IF_NOT_S_OK(hr);

                    ::SendMessageW(m_hwndDebugPipeHandler, WM_FORCEDISABLEDYNAMICDEBUGGING, NULL, NULL);

                    m_eventHelper.Fire_Event(L"onPdmClose", nullptr, 0);
                }
                break;
            }

        }
    }

    return S_OK;
}

HRESULT CDebuggerDispatch::OnEventBreakpoint(_In_ const bool isStarting, _In_ const CString& eventType)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spSourceController.p != nullptr, E_NOT_VALID_STATE);
    ATLENSURE_RETURN_HR(m_spThreadController.p != nullptr, E_NOT_VALID_STATE);

    HRESULT hr = S_OK;

    if (isStarting)
    {
        bool isMappedEventType = false;
        hr = m_spSourceController->GetIsMappedEventType(eventType, isMappedEventType);
        BPT_FAIL_IF_NOT_S_OK(hr);

        if (isMappedEventType)
        {
            hr = m_spThreadController->SetNextEvent(eventType);
            BPT_FAIL_IF_NOT_S_OK(hr);

            hr = m_spThreadController->Pause();
        }
    }
    else
    {
        hr = m_spThreadController->SetNextEvent(L"");
    }

    return hr;
}

// IScriptEventProvider
STDMETHODIMP CDebuggerDispatch::addEventListener(_In_ BSTR eventName, _In_ IDispatch* listener)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(listener != nullptr, E_INVALIDARG);

    return m_eventHelper.addEventListener(eventName, listener);
}

STDMETHODIMP CDebuggerDispatch::removeEventListener(_In_ BSTR eventName, _In_ IDispatch* listener)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(listener != nullptr, E_INVALIDARG);

    return m_eventHelper.removeEventListener(eventName, listener);
}

STDMETHODIMP CDebuggerDispatch::isEventListenerAttached(_In_ BSTR eventName, _In_opt_ IDispatch* listener, _Out_ ULONG* pAttachedCount)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pAttachedCount != nullptr, E_INVALIDARG);

    return m_eventHelper.isEventListenerAttached(eventName, listener, pAttachedCount);
}

STDMETHODIMP CDebuggerDispatch::removeAllEventListeners()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    return m_eventHelper.removeAllEventListeners();
}

// IF12DebuggerDispatch
STDMETHODIMP CDebuggerDispatch::enable(_Out_ VARIANT_BOOL* pSuccess)
{
    // Called to switch into debug mode by enabling dynamic debugging
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    // Call through to the IE thread to enable debugging
    LRESULT result = -1;
    if (m_hwndDebugPipeHandler != nullptr)
    {
        result = ::SendMessageW(m_hwndDebugPipeHandler, WM_ENABLEDYNAMICDEBUGGING, NULL, NULL);
    }

    (*pSuccess) = (result == 0 ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::disable(_Out_ VARIANT_BOOL* pSuccess)
{
    // Called to switch back into source rundown mode from debug mode
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    // Call through to the IE thread to disable debugging
    LRESULT result = -1;
    if (m_hwndDebugPipeHandler != nullptr)
    {
        result = ::SendMessageW(m_hwndDebugPipeHandler, WM_DISABLEDYNAMICDEBUGGING, NULL, NULL);
    }

    (*pSuccess) = (result == 0 ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::isEnabled(_Out_ VARIANT_BOOL* pIsEnabled)
{
    // Returns true if debugging is enabled or false if we are still in source rundown mode
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pIsEnabled != nullptr, E_INVALIDARG);

    (*pIsEnabled) = (m_spThreadController->IsEnabled() ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::connect(_In_ BOOL enable, _Out_ LONG* pResult)
{
    try
    {
        // Attaches our native code to the PDM as the active debugger
        ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
        ATLENSURE_RETURN_HR(pResult != nullptr, E_INVALIDARG);

        HRESULT hr = m_spThreadController->Connect();

        // Return the result of the connection attempt
        switch (hr)
        {
        case S_OK:
            (*pResult) = (LONG)ConnectionResult::Succeeded;
            m_spThreadController->SetEnabledState(!!enable);
            break;

        case E_ALREADY_ATTACHED:
            (*pResult) = (LONG)ConnectionResult::FailedAlreadyAttached;
            break;

        default:
            (*pResult) = (LONG)ConnectionResult::Failed;
            break;
        }
    }
    catch (const bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const CAtlException& err)
    {
        return err.m_hr;
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::disconnect(_Out_ VARIANT_BOOL* pSuccess)
{
    try
    {
        // Detaches our debugger from the PDM
        ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
        ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

        HRESULT hr = m_spThreadController->Disconnect();

        (*pSuccess) = (hr == S_OK ? VARIANT_TRUE : VARIANT_FALSE);
    }
    catch (const bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const CAtlException& err)
    {
        return err.m_hr;
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::shutdown(_Out_ VARIANT_BOOL* pSuccess)
{
    // Cleans up all references and shuts down the message queue
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    // At this point we should already have moved into source rundown mode so now
    // we disconnect the debugger and clean up
    HRESULT hr = disconnect(pSuccess);
    ATLENSURE_RETURN_HR(hr == S_OK && *pSuccess != VARIANT_FALSE, E_FAIL);

    this->removeAllEventListeners();

    m_spThreadController.Release();
    m_spSourceController.Release();
    m_spMessageQueue.Release();

    (*pSuccess) = (hr == S_OK ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::isConnected(_Out_ VARIANT_BOOL* pIsConnected)
{
    // Returns true if we have attached the the PDM
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pIsConnected != nullptr, E_INVALIDARG);

    (*pIsConnected) = (m_spThreadController->IsConnected() ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::causeBreak(_In_ ULONG causeBreakAction, _In_ ULONG /* workerId */, _Out_ VARIANT_BOOL* pSuccess)
{
    // Causes the PDM to break on the next executed line of script (if there is one)

    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    HRESULT hr = S_OK;

    if (!m_hasHitBreak) // Since the frontend doesn't yet have an concept of break id, we use this flag to determine if they are trying to break before resuming
    {
        switch ((CauseBreakAction)causeBreakAction)
        {
        case CauseBreakAction::BreakOnAny:
            // Cause an async break in the debugger when the next line of script executes
            hr = m_spThreadController->Pause();
            break;

        case CauseBreakAction::BreakOnAnyNewWorkerStarting:
            hr = m_spThreadController->SetBreakOnNewWorker(true);
            break;

        case CauseBreakAction::BreakIntoSpecificWorker:
            // Not implemented
            break;

        case CauseBreakAction::UnsetBreakOnAnyNewWorkerStarting:
            hr = m_spThreadController->SetBreakOnNewWorker(false);
            break;

        default:
            ATLENSURE_RETURN_HR(false, E_INVALIDARG);
            break;
        }
    }

    (*pSuccess) = (hr == S_OK ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::resume(_In_ ULONG breakResumeAction, _Out_ VARIANT_BOOL* pSuccess)
{
    // Causes the PDM to continue execution with the specified options
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    HRESULT hr = E_NOT_VALID_STATE;

    if (m_hasHitBreak) // Since the frontend doesn't yet have an concept of break id, we use this flag to determine if they are trying to resume a previous break
    {
        hr = m_spThreadController->Resume((BREAKRESUMEACTION)breakResumeAction);
        m_hasHitBreak = !(hr == S_OK);
    }

    (*pSuccess) = (hr == S_OK ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::addCodeBreakpoint(_In_ ULONG docId, _In_ ULONG start, _In_ BSTR condition, _In_ BOOL isTracepoint, _Out_ VARIANT* pvActualBp)
{
    try
    {
        // Adds a breakpoint to the specified document, at the given location.
        // The return value specifies the breakpoint id that can be later used in a call to removeBreakpoint to identify it.
        ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
        ATLENSURE_RETURN_HR(pvActualBp != nullptr, E_INVALIDARG);

        // Give the out parameter a blank value before it is used in this function
        ::VariantInit(pvActualBp);

        shared_ptr<BreakpointInfo> spAddedBreakpoint;
        CComBSTR newCondition(condition);
        HRESULT hr = m_spSourceController->AddCodeBreakpoint(docId, start, newCondition, !!isTracepoint, spAddedBreakpoint);
        if (hr == S_OK)
        {
            // Create the breakpoint JavaScript object
            CComVariant bpObject;
            hr = this->CreateScriptBreakpointInfo(*spAddedBreakpoint, bpObject);
            BPT_FAIL_IF_NOT_S_OK(hr);

            hr = bpObject.Detach(pvActualBp);
            BPT_FAIL_IF_NOT_S_OK(hr);
        }
        else
        {
            // Failure to add a breakpoint here, so return 'null' to the JavaScript
            CComVariant nullObject;
            nullObject.ChangeType(VT_NULL, nullptr);

            hr = nullObject.Detach(pvActualBp);
            BPT_FAIL_IF_NOT_S_OK(hr);
        }
    }
    catch (const bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const CAtlException& err)
    {
        return err.m_hr;
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::addEventBreakpoint(_In_ VARIANT* pvEventTypes, _In_ BOOL isEnabled, _In_ BSTR condition, _In_ BOOL isTracepoint, _Out_ VARIANT* pvActualBp)
{
    try
    {
        // Adds a breakpoint to the specified document, at the given location.
        // The return value specifies the breakpoint id that can be later used in a call to removeBreakpoint to identify it.
        ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
        ATLENSURE_RETURN_HR(pvActualBp != nullptr, E_INVALIDARG);
        ATLENSURE_RETURN_HR(pvEventTypes != nullptr && (pvEventTypes->vt == VT_NULL || (pvEventTypes->vt == VT_DISPATCH && pvEventTypes->pdispVal != nullptr)), E_INVALIDARG);

        // Give the out parameter a blank value before it is used in this function
        ::VariantInit(pvActualBp);

        shared_ptr<BreakpointInfo> spAddedBreakpoint;
        shared_ptr<vector<CComBSTR>> spEventTypes;
        if (pvEventTypes->vt == VT_DISPATCH)
        {
            spEventTypes.reset(new vector<CComBSTR>());
            vector<CComVariant> variantEventTypesVector;
            HRESULT hr = ScriptHelpers::GetVectorFromScriptArray(*pvEventTypes, variantEventTypesVector);
            BPT_FAIL_IF_NOT_S_OK(hr);

            for (const auto& item : variantEventTypesVector)
            {
                ATLENSURE_RETURN_HR(item.vt == VT_BSTR, E_INVALIDARG);
                CComBSTR value(item.bstrVal);
                spEventTypes->push_back(value);
            }
        }

        CComBSTR newCondition(condition);
        HRESULT hr = m_spSourceController->AddEventBreakpoint(spEventTypes, !!isEnabled, newCondition, !!isTracepoint, spAddedBreakpoint);
        if (hr == S_OK)
        {
            // Create the breakpoint JavaScript object
            CComVariant bpObject;
            hr = this->CreateScriptBreakpointInfo(*spAddedBreakpoint, bpObject);
            BPT_FAIL_IF_NOT_S_OK(hr);

            hr = bpObject.Detach(pvActualBp);
            BPT_FAIL_IF_NOT_S_OK(hr);
        }
        else
        {
            // Failure to add a breakpoint here, so return 'null' to the JavaScript
            CComVariant nullObject;
            nullObject.ChangeType(VT_NULL, nullptr);

            hr = nullObject.Detach(pvActualBp);
            BPT_FAIL_IF_NOT_S_OK(hr);
        }
    }
    catch (const bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const CAtlException& err)
    {
        return err.m_hr;
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::addPendingBreakpoint(_In_ BSTR url, _In_ ULONG start, _In_ BSTR condition, _In_ BOOL isEnabled, _In_ BOOL isTracepoint, _Out_ ULONG* pBreakpointId)
{
    HRESULT hr;

    try
    {
        // Adds a pending breakpoint at a given url, which is not bound to the backend. They get bound when a document with that url is added.
        // Returns id of the new breakpoint added.
        ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
        ATLENSURE_RETURN_HR(pBreakpointId != nullptr, E_INVALIDARG);
        ATLASSERT(::SysStringLen(url) > 0);

        CComBSTR newUrl(url);
        CComBSTR newCondition(condition);
        ULONG bpId = 0;
        hr = m_spSourceController->AddPendingBreakpoint(newUrl, start, newCondition, !!isEnabled, !!isTracepoint, bpId);

        (*pBreakpointId) = (hr == S_OK) ? bpId : 0;
    }
    catch (const bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const CAtlException& err)
    {
        return err.m_hr;
    }

    return hr;
}

STDMETHODIMP CDebuggerDispatch::removeBreakpoint(_In_ ULONG breakpointId, _Out_ VARIANT_BOOL* pSuccess)
{
    // Removes a breakpoint using the speicifed breakpoint id
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    HRESULT hr = m_spSourceController->RemoveBreakpoint(breakpointId);
    (*pSuccess) = (hr == S_OK ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::updateBreakpoint(_In_ ULONG breakpointId, _In_ BSTR condition, _In_ BOOL isTracepoint, _Out_ VARIANT_BOOL* pSuccess)
{
    // Updates an existing breakpoint to have a different condition or tracepoint value
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    CComBSTR newCondition(condition);
    HRESULT hr = m_spSourceController->UpdateBreakpoint(breakpointId, newCondition, !!isTracepoint);
    (*pSuccess) = (hr == S_OK ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::setBreakpointEnabledState(_In_ ULONG breakpointId, _In_ BOOL enable, _Out_ VARIANT_BOOL* pSuccess)
{
    // Toggles the state of an existing a breakpoint
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    HRESULT hr = m_spSourceController->SetBreakpointEnabledState(breakpointId, !!enable);
    (*pSuccess) = (hr == S_OK ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::getBreakpointIdFromSourceLocation(_In_ ULONG docId, _In_ ULONG start, _Out_ ULONG* pBreakpointId)
{
    // Gets the breakpointId corrisponding to the breakpoint set at the specified document and location,
    // It will return 0 if there is no existing breakpoint found at that location.
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pBreakpointId != nullptr, E_INVALIDARG);

    ULONG existingId = 0;
    HRESULT hr = m_spSourceController->GetBreakpointId(docId, start, existingId);
    if (hr == S_OK)
    {
        // Return the valid breakpoint id
        (*pBreakpointId) = existingId;
    }
    else
    {
        // Could not find an existing breakpoint here so return 0 (meaning invalid)
        (*pBreakpointId) = 0; 
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::getThreadDescription(_Out_ BSTR* pThreadDescription)
{
    // Gets a description of the current thread while at a breakpoint
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pThreadDescription != nullptr, E_INVALIDARG);

    CComBSTR description;
    HRESULT hr = m_spThreadController->GetCurrentThreadDescription(description);
    if (hr == S_OK)
    {
        // Copy the text into the out parameter
        hr = description.CopyTo(pThreadDescription);
        ATLASSERT(hr == S_OK); hr;
    }
    else
    {
        // Return an empty string instead instead of a bad HRESULT to avoid throwing an exceptions to the JavaScript
        (*pThreadDescription) = ::SysAllocString(L"");
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::getThreads(_Out_ VARIANT* pvThreadDescriptions)
{
    try
    {
        // Get the current executing threads (such as webworkers)
        ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
        ATLENSURE_RETURN_HR(pvThreadDescriptions != nullptr, E_INVALIDARG);

        vector<CComVariant> descriptions;

        vector<CComBSTR> spThreadDescriptions;
        HRESULT hr = m_spThreadController->GetThreadDescriptions(spThreadDescriptions);
        if (hr != S_OK)
        {
            for (const auto& desc : spThreadDescriptions)
            {
                descriptions.push_back(CComVariant(desc));
            }
        }
        else
        {
            // Return an empty array to the JavaScript
            descriptions.clear();
        }

        // Create the VARIANT that represents the array in JavaScript
        CComVariant descriptionsArray;
        hr = ScriptHelpers::CreateJScriptArray(m_spScriptDispatch, descriptions, descriptionsArray);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Set it to the out param
        ::VariantInit(pvThreadDescriptions);
        hr = descriptionsArray.Detach(pvThreadDescriptions);
        BPT_FAIL_IF_NOT_S_OK(hr);
    }
    catch (const bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const CAtlException& err)
    {
        return err.m_hr;
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::getFrames(_In_ LONG /* framesNeeded */, _Out_ VARIANT* pvFramesArray)
{
    try
    {
        // Get the current callstack when stopped at a breakpoint
        ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
        ATLENSURE_RETURN_HR(pvFramesArray != nullptr, E_INVALIDARG);

        vector<CComVariant> currentFrames;

        vector<shared_ptr<CallFrameInfo>> spFrames;
        HRESULT hr = m_spThreadController->GetCallFrames(spFrames);
        if (hr == S_OK)
        {
            for (const auto& frame : spFrames)
            {
                // Create the script location
                map<CString, CComVariant> locationMap;
                locationMap[L"docId"] = frame->sourceLocation.docId;
                locationMap[L"start"] = frame->sourceLocation.charPosition;
                locationMap[L"length"] = frame->sourceLocation.contextCount;

                CComVariant locationObject;
                hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, locationMap, locationObject);
                BPT_FAIL_IF_NOT_S_OK(hr);

                // Create the frame object and add the location
                map<CString, CComVariant> frameMap;
                frameMap[L"callFrameId"] = frame->id;
                frameMap[L"functionName"] = frame->functionName;
                frameMap[L"isInTryBlock"] = frame->isInTryBlock;
                frameMap[L"isInternal"] = frame->isInternal;
                frameMap[L"location"] = locationObject;

                CComVariant frameObject;
                hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, frameMap, frameObject);
                if (hr == S_OK)
                {
                    currentFrames.push_back(frameObject);
                }
            }
        }
        else
        {
            // Return an empty callstack array to the JavaScript
            currentFrames.clear();
        }

        // Create the VARIANT that represents the array in JavaScript
        CComVariant framesArray;
        hr = ScriptHelpers::CreateJScriptArray(m_spScriptDispatch, currentFrames, framesArray);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Set it to the out param
        ::VariantInit(pvFramesArray);
        hr = framesArray.Detach(pvFramesArray);
        BPT_FAIL_IF_NOT_S_OK(hr);
    }
    catch (const bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const CAtlException& err)
    {
        return err.m_hr;
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::getSourceText(_In_ ULONG docId, _Out_ VARIANT* pvSourceTextResult)
{
    try
    {
        // Returns a IGetSourceTextResult for the given document id.
        // The text should only be requested when this function is called, to prevent caching text if the front end is never going to show it.
        ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
        ATLENSURE_RETURN_HR(pvSourceTextResult != nullptr, E_INVALIDARG);

        CComBSTR sourceText;
        HRESULT hr = m_spSourceController->GetSourceText(docId, sourceText);
        bool loadFailed = !SUCCEEDED(hr);

        // Map that array onto an object with additional members
        map<CString, CComVariant> propertiesMap;
        propertiesMap[L"loadFailed"] = loadFailed;
        propertiesMap[L"text"] = loadFailed ? L"" : sourceText;

        // Create the VARIANT that represents that object in JavaScript
        CComVariant propertiesObject;
        hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, propertiesMap, propertiesObject);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Set it to the out param
        ::VariantInit(pvSourceTextResult);
        hr = propertiesObject.Detach(pvSourceTextResult);
        BPT_FAIL_IF_NOT_S_OK(hr);
    }
    catch (const bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const CAtlException& err)
    {
        return err.m_hr;
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::getLocals(_In_ ULONG frameId, _Out_ ULONG* pPropertyId)
{
    try
    {
        // Get the local variables associated with a callstack frame when at a breakpoint
        ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
        ATLENSURE_RETURN_HR(pPropertyId != nullptr, E_INVALIDARG);

        ULONG propertyId;
        HRESULT hr = m_spThreadController->GetLocals(frameId, propertyId);
        if (hr == S_OK)
        {
            // Set the valid property id to the out parameter
            (*pPropertyId) = propertyId;
        }
        else
        {
            // No property id
            (*pPropertyId) = 0;
        }
    }
    catch (const bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const CAtlException& err)
    {
        return err.m_hr;
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::eval(_In_ ULONG frameId, _In_ BSTR evalString, _Out_ VARIANT* pvPropertyInfo)
{
    try
    {
        // Evaluate an expression using the current breakpoint context
        ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
        ATLENSURE_RETURN_HR(evalString != nullptr, E_INVALIDARG);
        ATLENSURE_RETURN_HR(pvPropertyInfo != nullptr, E_INVALIDARG);

        // Give the out parameter a blank value before it is used in this function
        ::VariantInit(pvPropertyInfo);

        shared_ptr<PropertyInfo> spPropertyInfo;
        HRESULT hr = m_spThreadController->Eval(frameId, CComBSTR(evalString), EVAL_RADIX, spPropertyInfo);
        if (hr == S_OK)
        {
            CComVariant propertyInfoObject;
            hr = this->GetPropertyObjectFromPropertyInfo(spPropertyInfo, propertyInfoObject);
            BPT_FAIL_IF_NOT_S_OK(hr);

            hr = propertyInfoObject.Detach(pvPropertyInfo);
            BPT_FAIL_IF_NOT_S_OK(hr);
        }
        else
        {
            // No result, so return 'null' to the JavaScript
            CComVariant nullObject;
            nullObject.ChangeType(VT_NULL, nullptr);

            hr = nullObject.Detach(pvPropertyInfo);
            BPT_FAIL_IF_NOT_S_OK(hr);
        }
    }
    catch (const bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const CAtlException& err)
    {
        return err.m_hr;
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::getChildProperties(_In_ ULONG propertyId, _In_ ULONG start, _In_ ULONG length, _Out_ VARIANT* pvPropertyInfosObject)
{
    try
    {
        // Get the children of a valid variable when at a breakpoint
        ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
        ATLENSURE_RETURN_HR(pvPropertyInfosObject != nullptr, E_INVALIDARG);

        vector<CComVariant> properties;
        bool hasAdditionalChildren = false;

        vector<shared_ptr<PropertyInfo>> spPropertyInfos;
        HRESULT hr = m_spThreadController->EnumeratePropertyMembers(propertyId, EVAL_RADIX, spPropertyInfos);

        if (hr == S_OK && spPropertyInfos.size() < ULONG_MAX && start < (ULONG)spPropertyInfos.size())
        {
            ULONG total = (ULONG)spPropertyInfos.size();

            if (length == 0)
            {
                // Length 0 means to return all of the properties
                length = total;
            }

            ULONG end = start + length;
            if (end < total)
            {
                hasAdditionalChildren = true;
            }

            // Populate the list of properties
            for (ULONG i = start; i < total && i < end; i++)
            {
                CComVariant propertyInfoObject;
                hr = this->GetPropertyObjectFromPropertyInfo(spPropertyInfos[i], propertyInfoObject);
                BPT_FAIL_IF_NOT_S_OK(hr);

                properties.push_back(propertyInfoObject);
            }
        }
        else
        {
            // Return an empty array to the JavaScript
            properties.clear();
        }

        // Create the VARIANT that represents the array in JavaScript
        CComVariant propertiesArray;
        hr = ScriptHelpers::CreateJScriptArray(m_spScriptDispatch, properties, propertiesArray);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Map that array onto an object with additional members
        map<CString, CComVariant> propertiesMap;
        propertiesMap[L"hasAdditionalChildren"] = hasAdditionalChildren;
        propertiesMap[L"propInfos"] = propertiesArray;

        // Create the VARIANT that represents that object in JavaScript
        CComVariant propertiesObject;
        hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, propertiesMap, propertiesObject);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Set it to the out param
        ::VariantInit(pvPropertyInfosObject);
        hr = propertiesObject.Detach(pvPropertyInfosObject);
        BPT_FAIL_IF_NOT_S_OK(hr);
    }
    catch (const bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const CAtlException& err)
    {
        return err.m_hr;
    }

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::setPropertyValueAsString(_In_ ULONG propertyId, _In_ BSTR* pValue, _Out_ VARIANT_BOOL* pSuccess)
{
    // Sets the value of a IDebugProperty given by propertyId
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pValue != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    HRESULT hr = m_spThreadController->SetPropertyValueAsString(propertyId, pValue, EVAL_RADIX);
    (*pSuccess) = (hr == S_OK ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::canSetNextStatement(_In_ ULONG docId, _In_ ULONG position, _Out_ VARIANT_BOOL* pSuccess)
{
    // Tests to see if the PDM can move the instruction pointer to the specified location while at a breakpoint
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    HRESULT hr = m_spThreadController->CanSetNextStatement(docId, position);
    (*pSuccess) = (hr == S_OK ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::setNextStatement(_In_ ULONG docId, _In_ ULONG position, _Out_ VARIANT_BOOL* pSuccess)
{
    // Causes the PDM to move the instruction pointer to the specified location while at a breakpoint
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    HRESULT hr = m_spThreadController->SetNextStatement(docId, position);
    if (hr == S_OK) 
    {
        // StepOver action moves execution from the current IP to the point where is should be next time (as per the user sets it).
        // We send in ERRORRESUMEACTION_SkipErrorStatement in case this step was during an exception break. In that case we want to ignore the exception
        // and step directly.
        hr = m_spThreadController->Resume(BREAKRESUMEACTION_STEP_OVER, ERRORRESUMEACTION_SkipErrorStatement);
    }

    (*pSuccess) = (hr == S_OK ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

STDMETHODIMP CDebuggerDispatch::setBreakOnFirstChanceExceptions(_In_ BOOL fValue, _Out_ VARIANT_BOOL* pSuccess)
{
    // Causes the PDM to change its exception settings
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSuccess != nullptr, E_INVALIDARG);

    HRESULT hr = m_spThreadController->SetBreakOnFirstChanceExceptions(!!fValue);
    (*pSuccess) = (hr == S_OK ? VARIANT_TRUE : VARIANT_FALSE);

    return S_OK;
}

// Helper functions
HRESULT CDebuggerDispatch::OnEventListenerUpdated(_In_ bool const isListening)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spSourceController.p != nullptr, E_NOT_VALID_STATE);

    HRESULT hr = S_OK;

    if (isListening)
    {
        m_spSourceController->RebindAllEventBreakpoints(/*isEventListenerRegistered=*/ true);
    }
    else
    {
        // Now that the listeners have been removed in the BHO, unbind all the event breakpoints
        m_spSourceController->UnbindAllEventBreakpoints(/*removeListeners=*/ false);
    }

    return hr;
}

HRESULT CDebuggerDispatch::FireDocumentEvent(_In_ const CString& eventName, _In_ vector<CComVariant>& objects)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    HRESULT hr = S_OK;

    if (objects.empty())
    {
        return hr;
    }

    CComVariant object;

    hr = ScriptHelpers::CreateJScriptArray(m_spScriptDispatch, objects, object);
    objects.clear();
    BPT_FAIL_IF_NOT_S_OK(hr);

    const UINT argLength = 1;
    CComVariant params[argLength] = { object };

    m_eventHelper.Fire_Event(eventName, params, argLength);

    return hr;
}

HRESULT CDebuggerDispatch::GetPropertyObjectFromPropertyInfo(_In_ const shared_ptr<PropertyInfo>& spPropertyInfo, _Out_ CComVariant& propertyInfoObject)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Copy the strings into the map object
    map<CString, CComVariant> mapNameValuePairs;
    mapNameValuePairs[L"propertyId"] = spPropertyInfo->propertyId;
    mapNameValuePairs[L"name"] = spPropertyInfo->name;
    mapNameValuePairs[L"type"] =spPropertyInfo->type;
    mapNameValuePairs[L"value"] = spPropertyInfo->value;
    mapNameValuePairs[L"fullName"] = spPropertyInfo->fullName;
    mapNameValuePairs[L"expandable"] = spPropertyInfo->isExpandable;
    mapNameValuePairs[L"readOnly"] = spPropertyInfo->isReadOnly;
    mapNameValuePairs[L"fake"] = spPropertyInfo->isFake;
    mapNameValuePairs[L"invalid"] = spPropertyInfo->isInvalid;
    mapNameValuePairs[L"returnValue"] = spPropertyInfo->isReturnValue;

    // Create the JavaScript object
    HRESULT hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, mapNameValuePairs, propertyInfoObject);
    BPT_FAIL_IF_NOT_S_OK(hr);

    return S_OK;
} 

HRESULT CDebuggerDispatch::CreateScriptBreakpointInfo(_In_ const BreakpointInfo& breakpoint, _Out_ CComVariant& breakpointObject)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    HRESULT hr = breakpointObject.Clear();
    BPT_FAIL_IF_NOT_S_OK(hr);

    map<CString, CComVariant> bpMap;
    bpMap[L"breakpointId"] = breakpoint.id;
    auto sourceLocation = breakpoint.spSourceLocation;
    if (sourceLocation != nullptr)
    {
        map<CString, CComVariant> sourceLocationMap;
        sourceLocationMap[L"docId"] = sourceLocation->docId;
        sourceLocationMap[L"start"] = sourceLocation->charPosition;
        sourceLocationMap[L"length"] = sourceLocation->contextCount;
        CComVariant sourceLocationObject;
        hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, sourceLocationMap, sourceLocationObject);
        BPT_FAIL_IF_NOT_S_OK(hr);

        bpMap[L"location"] = sourceLocationObject;
    }

    auto eventTypes = breakpoint.spEventTypes;
    if (eventTypes != nullptr)
    {
        CComVariant scriptEventTypes;
        // Convert the vector<CComBSTR> to vector<CComVariant>
        vector<CComVariant> variantEventTypes;
        for (const auto& item : *eventTypes)
        {
            CComBSTR newItem(item);
            variantEventTypes.push_back(CComVariant(newItem));
        }

        hr = ScriptHelpers::CreateJScriptArray(m_spScriptDispatch, variantEventTypes, scriptEventTypes);
        BPT_FAIL_IF_NOT_S_OK(hr);
        bpMap[L"eventTypes"] = scriptEventTypes;
    }

    bpMap[L"condition"] = breakpoint.condition;
    bpMap[L"isTracepoint"] = breakpoint.isTracepoint;
    bpMap[L"isEnabled"] = breakpoint.isEnabled;
    bpMap[L"isBound"] = breakpoint.isBound;

    hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, bpMap, breakpointObject);
    BPT_FAIL_IF_NOT_S_OK(hr);

    return hr;
}

HRESULT CDebuggerDispatch::CreateSetMutationBreakpointResult(_In_ bool success, _In_ ULONG breakpointId, _In_ CComBSTR& objectName, _Out_ CComVariant& resultObject)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Copy the strings into the map object
    map<CString, CComVariant> resultMap;
    resultMap[L"success"] = success;
    resultMap[L"breakpointId"] = breakpointId;
    resultMap[L"objectName"] = objectName;

    // Create the JavaScript object
    HRESULT hr = ScriptHelpers::CreateJScriptObject(m_spScriptDispatch, resultMap, resultObject);
    BPT_FAIL_IF_NOT_S_OK(hr);

    return S_OK;
}
