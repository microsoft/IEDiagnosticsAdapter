//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "BrowserMessageQueue.h"

BrowserMessageQueue::BrowserMessageQueue() :
    m_isAppAdvised(false),
    m_isThreadAdvised(false),
    m_isValid(false),
    m_notifyOnBreak(false),
    m_appCookie(0),
    m_threadCookie(0),
    m_messageWindow(NULL),
    m_safeToEvalScriptDuringDebugThreadCall(true)
{
}

void BrowserMessageQueue::Initialize(_In_ IDebugApplication110* pDebugApplication, _In_ IDebugThreadCall* pCall, _In_ HWND messageWnd, bool notifyOnBreak)
{
    m_spDebugApplication = pDebugApplication;
    m_spCall = pCall;
    m_messageWindow = messageWnd;
    m_notifyOnBreak = notifyOnBreak;

    // Must be called on the browser ui thread
    m_mainThreadId = ::GetCurrentThreadId();

    if (pDebugApplication)
    {
        HRESULT hr = ::AtlAdvise(m_spDebugApplication, GetUnknown(), IID_IRemoteDebugApplicationEvents, &m_appCookie);
        ATLASSERT(hr == S_OK);
        if (hr == S_OK) 
        { 
            m_isAppAdvised = true; 
        }

        this->TryMainThreadAdvise();
    }

    m_isValid = true;
}

void BrowserMessageQueue::Deinitialize()
{
    if (m_isValid)
    {
        // Should be deinitialized on the browser ui thread
        ATLASSERT(::GetCurrentThreadId() == m_mainThreadId);

        // Scope for the objects lock
        {
            CComCritSecLock<CComAutoCriticalSection> lock(m_csCOMObjects);
            m_isValid = false;
            if (m_isAppAdvised)
            {
                HRESULT hr = ::AtlUnadvise(m_spDebugApplication, IID_IRemoteDebugApplicationEvents, m_appCookie);
                ATLASSERT(hr == S_OK); hr;
            }

            if (m_isThreadAdvised)
            {
                CComPtr<IRemoteDebugApplicationThread> spMainThread;
                HRESULT hr = m_spDebugApplication->GetMainThread(&spMainThread);
                if (spMainThread != nullptr)
                {
                    hr = ::AtlUnadvise(spMainThread, __uuidof(IDebugApplicationThreadEvents110), m_threadCookie);
                    ATLASSERT(hr == S_OK); hr;
                }
            }

            m_spCall.Release();
            m_spDebugApplication.Release();
            m_messageWindow = nullptr;
        }

        // Scope for the messages lock
        {
            CComCritSecLock<CComAutoCriticalSection> lock2(m_csMessageArray);
            m_messages.clear();
        }
    }
}

void BrowserMessageQueue::Push(_In_ shared_ptr<MessagePacket> spMessage)
{
    // Scope for the messages lock
    {
        CComCritSecLock<CComAutoCriticalSection> lock(m_csMessageArray);
        m_messages.push_back(spMessage);
    }

    HRESULT hr = this->TriggerThreadCall();
    if (hr != S_OK)
    {
        this->PostProcessPacketsMessage();
    }
}

void BrowserMessageQueue::PopAll(_Inout_ vector<shared_ptr<MessagePacket>>& messages)
{
    CComCritSecLock<CComAutoCriticalSection> lock(m_csMessageArray);

    messages.assign(m_messages.begin(), m_messages.end());
    m_messages.clear();
}

void BrowserMessageQueue::Remove(_In_ const shared_ptr<MessagePacket>& spMessage)
{
    CComCritSecLock<CComAutoCriticalSection> lock(m_csMessageArray);

    auto packetIt = find(m_messages.cbegin(), m_messages.cend(), spMessage);
    if (packetIt != m_messages.end())
    {
        m_messages.erase(packetIt);
    }
}

BOOL BrowserMessageQueue::PostProcessPacketsMessage(bool postIfAny)
{
    if (!m_isValid)
    {
        // It's possible for the message queue to contain messages after it is released. 
        // Simply ignore the messages as the queue finishes up.
        return FALSE;
    }

    vector<shared_ptr<MessagePacket>>::size_type size;

    // Scope for the lock
    {
        CComCritSecLock<CComAutoCriticalSection> lock(m_csMessageArray);
        size = m_messages.size();
    }

    // We only need to post a message to the other thread if either:
    // This is the first item placed in the queue 
    // - OR -
    // The queue has had several items placed in it without processing, due to being at a breakpoint
    if (size == 1 || (postIfAny && size > 0))
    {
        return ::PostMessage(m_messageWindow, WM_MESSAGE_IN_QUEUE, NULL, NULL);
    }

    return TRUE;
}

HRESULT BrowserMessageQueue::AsyncCallOnMainThread(DWORD_PTR messageID)
{
    if (m_spDebugApplication && m_spCall && m_safeToEvalScriptDuringDebugThreadCall)
    {
        CComCritSecLock<CComAutoCriticalSection> lock(m_csCOMObjects);

        if (m_spDebugApplication && m_spCall)
        {
            this->TryMainThreadAdvise();

            CComPtr<IRemoteDebugApplicationThread> spRemoteThread;
            HRESULT hr = m_spDebugApplication->GetMainThread(&spRemoteThread);
            if (hr != S_OK) { return hr; }

            CComQIPtr<IDebugApplicationThread110> spThread110(spRemoteThread);
            ATLENSURE_RETURN_HR(spThread110.p != NULL, E_NOINTERFACE);

            BOOL isCallable;
            hr = spThread110->IsThreadCallable(&isCallable);
            if (hr != S_OK) { return hr; }

            if (isCallable)
            {
                // Make sure we only issue the call while suspended at a breakpoint.
                // If we're not suspended, the call will be made in response to the suspended event.
                BOOL isSuspendedForBreak;
                hr = spThread110->IsSuspendedForBreakPoint(&isSuspendedForBreak);
                ATLENSURE_RETURN_HR(hr == S_OK, hr);

                if (isSuspendedForBreak)
                {
                    UINT cRequestsActive;
                    hr = spThread110->GetActiveThreadRequestCount(&cRequestsActive);

                    if (cRequestsActive > 0)
                    {
                        // Returning S_OK if there's an active request because we don't need to post message - 
                        // we'll requeue during OnThreadRequestComplete for that request
                        return S_OK;
                    }

                    CComObject<PassthroughDebugThreadCall>* pPassthroughDebugThreadCall;
                    hr = CComObject<PassthroughDebugThreadCall>::CreateInstance(&pPassthroughDebugThreadCall);
                    ATLENSURE_RETURN_HR(hr == S_OK, hr);

                    CComQIPtr<IDebugThreadCall> spDebugThreadCall(pPassthroughDebugThreadCall);
                    ATLENSURE_RETURN_HR(spDebugThreadCall.p != NULL, E_NOINTERFACE);

                    pPassthroughDebugThreadCall->Initialize(spThread110, m_spCall, messageID);

                    hr = spThread110->AsynchronousCallIntoThread(spDebugThreadCall, NULL, NULL, NULL);
                    return hr;
                }
            }
        }
    }

    return S_FALSE;
}

HRESULT BrowserMessageQueue::TriggerThreadCall()
{
    return this->AsyncCallOnMainThread(WM_MESSAGE_IN_QUEUE);
}

void BrowserMessageQueue::NotifyBreakOccurred()
{
    this->AsyncCallOnMainThread(WM_BREAK_OCCURRED);
}

STDMETHODIMP BrowserMessageQueue::OnEnterBreakPoint(__RPC__in_opt IRemoteDebugApplicationThread* prdat)
{
    // It's unsafe to evaluate script until onsuspendforbreakpoint is called since the debugger will be in an odd state.
    if (prdat)
    {
        DWORD applicationThread;
        HRESULT hr = prdat->GetSystemThreadId(&applicationThread);
        ATLASSERT(hr == S_OK);
        if (hr == S_OK && applicationThread == m_mainThreadId)
        {
            this->TryMainThreadAdvise();
            m_safeToEvalScriptDuringDebugThreadCall = false;
        }
    }

    return S_OK;
}

STDMETHODIMP BrowserMessageQueue::OnSuspendForBreakPoint()
{
    // This means the debugger has fully processed the breakpoint and is ready for things to happen.  
    // So, let's call process messageswithdebugger to see if we need to execute anything now that it's safe to do so.
    m_safeToEvalScriptDuringDebugThreadCall = true;
    
    this->ProcessMessagesWithDebugger();

    if (m_notifyOnBreak)
    {
        this->NotifyBreakOccurred();
    }

    return S_OK;
}

void BrowserMessageQueue::ProcessMessagesWithDebugger()
{
    bool shouldProcessMessages = false;

    // Determine the main thread id and the thread id that just broke. 
    // If they match, we need to process remaining messages that might 
    // have been post messaged but never processed.

    if (m_spDebugApplication && m_spCall)
    {
        DWORD eventThreadId = ::GetCurrentThreadId();

        if (eventThreadId == m_mainThreadId)
        {
            size_t cMessages = 0;

            // Scope for lock
            {
                CComCritSecLock<CComAutoCriticalSection> lock(m_csMessageArray);
                cMessages = m_messages.size();
            }

            if (cMessages != 0)
            {
                shouldProcessMessages = true;
            }
        }
    }

    if (shouldProcessMessages)
    {
        // Need to make sure anything still in the post message queue makes it
        // This call could fail to switch the the PDM thread and return S_FALSE, 
        // But in that case it must be because we have resumed from the breakpoint, 
        // So OnResumeFromBreakPoint will ensure we message the other thread to clear its queue.
        this->TriggerThreadCall();
    }
}

STDMETHODIMP BrowserMessageQueue::OnResumeFromBreakPoint()
{
    // Since several items may have been added into the queue without the PDM being ready to switch to the other thread,
    // We need to force a message to the other thread if there are any items in the queue at all.
    this->PostProcessPacketsMessage(/*postIfAny=*/true);

    return S_OK;
}

STDMETHODIMP BrowserMessageQueue::OnThreadRequestComplete()
{
    this->ProcessMessagesWithDebugger();

    return S_OK;
}

STDMETHODIMP BrowserMessageQueue::OnBeginThreadRequest()
{
    return S_OK;
}

STDMETHODIMP BrowserMessageQueue::OnLeaveBreakPoint(__RPC__in_opt IRemoteDebugApplicationThread* prdat)
{
    // Determine the main thread id and the thread id that just resumed. 
    // If they match, we need to process remaining messages that might 
    // have been queued for break mode processing but never processed.

    if (prdat)
    {
        DWORD eventThreadId;

        HRESULT hr = prdat->GetSystemThreadId(&eventThreadId);
        ATLENSURE_RETURN_HR(hr == S_OK, S_OK);

        if (eventThreadId == m_mainThreadId)
        {
            m_safeToEvalScriptDuringDebugThreadCall = true;

            // Scope for lock
            {
                CComCritSecLock<CComAutoCriticalSection> lock(m_csCOMObjects);
                if (m_messageWindow != NULL)
                {
                    // Queue a windows message to be process any remaining messages when the thread resumes
                    ::PostMessage(m_messageWindow, WM_MESSAGE_IN_QUEUE, NULL, NULL);
                }
            }
        }
    }
    return S_OK;
}

void BrowserMessageQueue::TryMainThreadAdvise()
{
    // Sometimes the main thread hasn't been activated in the PDM yet when we are initialized.
    // So we call this both when we first initialize, but also when we receive an enter break point event on an 
    // application thread to make sure we get a chance to advise before we enter break mode.
    if (m_isThreadAdvised) { return; }

    if (m_spDebugApplication.p != NULL)
    {
        CComPtr<IRemoteDebugApplicationThread> spThread;
        HRESULT hr = m_spDebugApplication->GetMainThread(&spThread);
        if (hr == S_OK)
        {
            hr = ::AtlAdvise(spThread, GetUnknown(), __uuidof(IDebugApplicationThreadEvents110), &m_threadCookie);
            ATLASSERT(hr == S_OK);

            if (hr == S_OK) 
            { 
                m_isThreadAdvised = true; 
            }
        }
    }
}

