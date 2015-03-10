//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "SourceEventListener.h"

SourceEventListener::SourceEventListener() :
    m_ownerId(0),
    m_dispatchThreadId(0),
    m_dwNodeCookie(0),
    m_dwTextCookie(0),
    m_advisedNodeEvents(false),
    m_advisedTextEvents(false)
{
    // We are created on the PDM thread
}

HRESULT SourceEventListener::Initialize(_In_ DWORD dispatchThreadId, _In_ SourceController* pSourceController, _In_ ULONG ownerId, _In_ IDebugApplicationNode* pDebugApplicationNode, _In_opt_ IDebugDocumentText* pDebugDocumentText)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSourceController != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(pDebugApplicationNode != nullptr, E_INVALIDARG);

    m_dispatchThreadId = dispatchThreadId;
    m_spSourceController = pSourceController;
    m_ownerId = ownerId;
    m_spDebugApplicationNode = pDebugApplicationNode;
    m_spDebugDocumentText = pDebugDocumentText;

    // Connect node event sink
    HRESULT hr = ATL::AtlAdvise(m_spDebugApplicationNode, GetUnknown(), IID_IDebugApplicationNodeEvents, &m_dwNodeCookie);
    m_advisedNodeEvents = (hr == S_OK);

    // Not all nodes have the IDebugDocumentText interface, depending on the host that is managing it.
    // We only want to sink text events if this interface exists, otherwise we can just safely ignore it.
    if (m_spDebugDocumentText.p != nullptr)
    {
        hr = ATL::AtlAdvise(m_spDebugDocumentText, GetUnknown(), IID_IDebugDocumentTextEvents, &m_dwTextCookie);
        m_advisedTextEvents = (hr == S_OK);
    }

    return S_OK;
}

HRESULT SourceEventListener::Deinitialize()
{
    HRESULT hr = S_OK;

    if (m_advisedTextEvents)
    {
        hr = ATL::AtlUnadvise(m_spDebugDocumentText, IID_IDebugDocumentTextEvents, m_dwTextCookie);
        m_advisedTextEvents = (hr != S_OK);
    }

    if (m_advisedNodeEvents)
    {
        HRESULT hrNode = ATL::AtlUnadvise(m_spDebugApplicationNode, IID_IDebugApplicationNodeEvents, m_dwNodeCookie);
        m_advisedNodeEvents = (hrNode != S_OK);
        if (hrNode != S_OK) 
        {
            // if either unadvise fails, it's probably a leak, so fail the disconnect
            hr = hrNode;
        }
    }

    if (hr == S_OK)
    {
        // Release our references
        m_spDebugDocumentText.Release();
        m_spDebugApplicationNode.Release();
        m_spSourceController.Release();
    }

    return hr;
}

// IDebugApplicationNodeEvents
HRESULT SourceEventListener::onAddChild(_In_ IDebugApplicationNode* prddpChild)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(prddpChild != nullptr, E_INVALIDARG);

    return m_spSourceController->OnAddChild(m_ownerId, prddpChild);
}

HRESULT SourceEventListener::onRemoveChild(_In_ IDebugApplicationNode* prddpChild)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(prddpChild != nullptr, E_INVALIDARG);

    return m_spSourceController->OnRemoveChild(m_ownerId, prddpChild);
}

HRESULT SourceEventListener::onDetach()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    return S_OK;
}

HRESULT SourceEventListener::onAttach(_In_ IDebugApplicationNode* /* prddpParent */)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    return S_OK;
}

// IDebugDocumentTextEvents
HRESULT SourceEventListener::onDestroy() 
{ 
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    return S_OK;
}

HRESULT SourceEventListener::onInsertText(_In_ ULONG /* cCharacterPosition */, _In_ ULONG /* cNumToInsert */)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    // Even if cNumToInsert is 0, we still need to forward the event to update breakpoint bindings.
    // Deduplication of successive update document events occurs in DebugProvider.
    return this->OnSourceTextChange();
}

HRESULT SourceEventListener::onRemoveText(_In_ ULONG /* cCharacterPosition */, _In_ ULONG /* cNumToRemove */)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    // Even if cNumToRemove is 0, we still need to forward the event to update breakpoint bindings.
    // Deduplication of successive update document events occurs in DebugProvider.
    return this->OnSourceTextChange();
}

HRESULT SourceEventListener::onReplaceText(_In_ ULONG /* cCharacterPosition */, _In_ ULONG /* cNumToReplace */)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    // Even if cNumToReplace is 0, we still need to forward the event to update breakpoint bindings.
    // Deduplication of successive update document events occurs in DebugProvider.
    return this->OnSourceTextChange();
}

HRESULT SourceEventListener::onUpdateTextAttributes(_In_ ULONG /* cCharacterPosition */, _In_ ULONG /* cNumToUpdate */)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    return S_OK;
}

HRESULT SourceEventListener::onUpdateDocumentAttributes(_In_ TEXT_DOC_ATTR /* textdocattr */)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    return S_OK;
}

// Helper functions
HRESULT SourceEventListener::OnSourceTextChange()
{
    return m_spSourceController->OnUpdate(m_ownerId);
}
