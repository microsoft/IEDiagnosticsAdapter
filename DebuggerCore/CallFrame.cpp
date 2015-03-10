//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "CallFrame.h"

CallFrame::CallFrame() :
    m_dispatchThreadId(0), // We are always created on the PDM thread so we get initialized with the dispatch thread id later
    m_id(0),
    m_isInternal(false)
{
}

HRESULT CallFrame::Initialize(_In_ DWORD dispatchThreadId, _In_ ULONG id, _In_ shared_ptr<DebugStackFrameDescriptor> spFrameDescriptor, _In_ SourceController* pSourceController)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(spFrameDescriptor != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(pSourceController != nullptr, E_INVALIDARG);

    m_dispatchThreadId = dispatchThreadId;
    m_id = id;
    m_spFrameDescriptor = spFrameDescriptor;
    m_spSourceController = pSourceController;

    // Populate the information
    HRESULT hr = this->UpdateInfo();
    BPT_FAIL_IF_NOT_S_OK(hr);

    return hr;
}

HRESULT CallFrame::UpdateInfo() 
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    m_isInternal = false;

    // Get the function name
    m_spFrameDescriptor->pdsf->GetDescriptionString(/*fLong=*/ false /* JS doesn't expose params */, &m_bstrFunctionName);

    // Populate the source location info
    m_spSourceLocationInfo.reset(new SourceLocationInfo());
    m_spSourceLocationInfo->docId = m_id;

    CComPtr<IDebugCodeContext> spDebugCodeContext;
    HRESULT hr = m_spFrameDescriptor->pdsf->GetCodeContext(&spDebugCodeContext);
    if (hr == S_OK)
    {
        bool isInternal = false;
        hr = m_spSourceController->GetSourceLocationForCallFrame(spDebugCodeContext, m_spSourceLocationInfo, isInternal);
        if (hr == E_NOT_FOUND)
        {
            // This call can fail if the source text has changed due to a document.write().
            // This can also fail for freed script, since chakra has already thrown away the script, we cannot get the location.
            // The GetSourceLocationForCallFrame() function should have populated the docId anyway with a temp one.
            // We know that this hr is valid for our call, so we return S_OK instead.
            hr = S_OK;
        }

        m_isInternal = isInternal;
    }

    return hr;
}

HRESULT CallFrame::GetCallFrameInfo(_Out_ shared_ptr<CallFrameInfo>& spFrameInfo, _Out_ CComPtr<IDebugStackFrame>& spStackFrame, _Out_ CComPtr<IDebugProperty>& spLocalsDebugProperty)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    spFrameInfo.reset(new CallFrameInfo());
    spFrameInfo->id = m_id;
    spFrameInfo->functionName = m_bstrFunctionName;
    spFrameInfo->isInTryBlock = false;
    spFrameInfo->isInternal = m_isInternal;
    spFrameInfo->sourceLocation = (*m_spSourceLocationInfo.get());

    spStackFrame = m_spFrameDescriptor->pdsf;

    HRESULT hr = m_spFrameDescriptor->pdsf->GetDebugProperty(&spLocalsDebugProperty);
    BPT_FAIL_IF_NOT_S_OK(hr);

    // Get the try block information
    DebugPropertyInfo propInfo;
    hr = spLocalsDebugProperty->GetPropertyInfo(DBGPROP_INFO_ATTRIBUTES, /*radix=*/ 10, &propInfo);
    if (hr == S_OK) 
    {
        spFrameInfo->isInTryBlock = ((propInfo.m_dwAttrib & DBGPROP_ATTRIB_FRAME_INTRYBLOCK) == DBGPROP_ATTRIB_FRAME_INTRYBLOCK);
    }

    return S_OK;
}
