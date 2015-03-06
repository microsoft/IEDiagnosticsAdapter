//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "PDMEventMessageQueue.h"
#include "DebugThreadWindowMessages.h"

PDMEventMessageQueue::PDMEventMessageQueue() :
    m_dispatchThreadId(::GetCurrentThreadId()), // We are always created on the dispatch thread
    m_hwndNotify(nullptr),
    m_isValid(false)
{
}

HRESULT PDMEventMessageQueue::Initialize(_In_ HWND hwndNotify)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    m_hwndNotify = hwndNotify;
    m_isValid = true;

    return S_OK;
}

HRESULT PDMEventMessageQueue::Deinitialize()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    if (m_isValid)
    {
        m_isValid = false;
        m_hwndNotify = nullptr;

        CComCritSecLock<CComAutoCriticalSection> lock(m_csMessageArray);
        m_messages.clear();
    }

    return S_OK;
}

void PDMEventMessageQueue::Push(_In_ PDMEventType eventType)
{
    ATLENSURE_RETURN_VAL(m_isValid, );

    vector<PDMEventType>::size_type size;

    {
        CComCritSecLock<CComAutoCriticalSection> lock(m_csMessageArray);
        m_messages.push_back(eventType);
        size = m_messages.size();
    }

    if (size == 1)
    {
        ::PostMessageW(m_hwndNotify, WM_PROCESSDEBUGGERPACKETS, NULL, NULL);
    }
}

void PDMEventMessageQueue::PopAll(_Out_ vector<PDMEventType>& messages)
{
    CComCritSecLock<CComAutoCriticalSection> lock(m_csMessageArray);

    messages.assign(m_messages.begin(), m_messages.end());
    m_messages.clear();
}
